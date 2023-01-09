/**
 * Framework for NoGo and similar games (C++ 11)
 * agent.h: Define the behavior of variants of the player
 *
 * Author: Theory of Computer Games
 *         Computer Games and Intelligence (CGI) Lab, NYCU, Taiwan
 *         https://cgilab.nctu.edu.tw/
 */

#pragma once
#include <string>
#include <random>
#include <sstream>
#include <map>
#include <type_traits>
#include <algorithm>
#include <fstream>
#include <functional>
#include "board.h"
#include "action.h"

class agent {
public:
	agent(const std::string& args = "") {
		std::stringstream ss("name=unknown role=unknown " + args);
		for (std::string pair; ss >> pair; ) {
			std::string key = pair.substr(0, pair.find('='));
			std::string value = pair.substr(pair.find('=') + 1);
			meta[key] = { value };
		}
	}
	virtual ~agent() {}
	virtual void open_episode(const std::string& flag = "") {}
	virtual void close_episode(const std::string& flag = "") {}
	virtual action take_action(const board& b) { return action(); }
	virtual bool check_for_win(const board& b) { return false; }

public:
	virtual std::string property(const std::string& key) const { return meta.at(key); }
	virtual void notify(const std::string& msg) { meta[msg.substr(0, msg.find('='))] = { msg.substr(msg.find('=') + 1) }; }
	virtual std::string name() const { return property("name"); }
	virtual std::string role() const { return property("role"); }

protected:
	typedef std::string key;
	struct value {
		std::string value;
		operator std::string() const { return value; }
		template<typename numeric, typename = typename std::enable_if<std::is_arithmetic<numeric>::value, numeric>::type>
		operator numeric() const { return numeric(std::stod(value)); }
	};
	std::map<key, value> meta;
};

/**
 * base agent for agents with randomness
 */
class random_agent : public agent {
public:
	random_agent(const std::string& args = "") : agent(args) {
		if (meta.find("seed") != meta.end())
			engine.seed(int(meta["seed"]));
	}
	virtual ~random_agent() {}

protected:
	std::default_random_engine engine;
};

/**
 * random player for both side
 * put a legal piece randomly
 */
class random_player : public random_agent {
public:
	random_player(const std::string& args = "") : random_agent("name=random role=unknown " + args),
		space(board::size_x * board::size_y), who(board::empty) {
		if (name().find_first_of("[]():; ") != std::string::npos)
			throw std::invalid_argument("invalid name: " + name());
		if (role() == "black") who = board::black;
		if (role() == "white") who = board::white;
		if (who == board::empty)
			throw std::invalid_argument("invalid role: " + role());
		for (size_t i = 0; i < space.size(); i++)
			space[i] = action::place(i, who);
	}

	virtual action take_action(const board& state) {
		std::shuffle(space.begin(), space.end(), engine);
		for (const action::place& move : space) {
			board after = state;
			if (move.apply(after) == board::legal)
				return move;
		}
		return action();
	}

protected:
	std::vector<action::place> space;
	board::piece_type who;
};

/**
 * player for both side
 * MCTS: perform N cycles and take the best action by visit count
 * random: put a legal piece randomly
 */
class player : public random_player {
public:
	player(const std::string& args = "") : random_player("name=player role=unknown search=MCTS simulation=0 " + args) {
		if (property("search") != "MCTS")
			throw std::invalid_argument("invalid search: " + property("search"));
		if (meta.find("timeout") != meta.end())
			throw std::invalid_argument("invalid option: timeout");
	}

	virtual action take_action(const board& state) {
		size_t N = meta["simulation"];
		if (N) return node(state).run_mcts(N, engine);
		return random_player::take_action(state);
	}

	class node : board { //
	public:
		node(const board& state, node* parent = nullptr) : board(state),
			win(0), visit(0), child(), parent(parent) {}

		/**
		 * run MCTS for N cycles and retrieve the best action
		 */
		action run_mcts(size_t N, std::default_random_engine& engine) {
			std::unordered_map<board::piece_type, std::unordered_map<int, int>> rave_win;
			std::unordered_map<board::piece_type, std::unordered_map<int, int>> rave_visit;
			for (size_t i = 0; i < N; i++) {
				std::vector<node*> path = select(rave_win, rave_visit);
				node* leaf = path.back()->expand(engine);.
				if (leaf != path.back()) path.push_back(leaf);
				update(path, leaf->simulate(engine), rave_win, rave_visit);
			}
			return take_action();
		}

	protected:

		/**
		 * select from the current node to a leaf node by UCB and return all of them
		 * a leaf node can be either a node that is not fully expanded or a terminal node
		 */
		std::vector<node*> select(
			std::unordered_map<board::piece_type, std::unordered_map<int, int>> rave_win,
			std::unordered_map<board::piece_type, std::unordered_map<int, int>> rave_visit) {
			std::vector<node*> path = { this };
			while(ndptr->is_selectable()){
				ndptr = &*std::max_element(ndptr->child.begin(), ndptr->child.end(),
						[=](const node& lhs, const node& rhs) { return lhs.ucb_score(rave_win, rave_visit) < 
																	   rhs.ucb_score(rave_win, rave_visit); });
				path.push_back(ndptr)
			}
			return path;
		}

		/**
		 * expand the current node and return the newly expanded child node
		 * if the current node has no unexpanded move, it returns itself
		 */

		node* expand(std::default_random_engine& engine) {
			board child_state = *this;
			std::vector<int> moves = all_moves(engine);
			auto expanded_move = std::find_if(moves.begin(), moves.end(), [&](int move) {
				// check whether it is an unexpanded legal move
				bool is_expanded = std::find_if(child.begin(), child.end(),
						[&](const node& node) { return node.info().last_move_index == move; }) != child.end(); //last_move_index is the 1-d position index of last move
				return is_expanded == false && child_state.place(move) == board::legal;
			});
			if (expanded_move == moves.end()) return this; // already terminal
			child.emplace_back(child_state, this);
			return &child.back();
		}

		/**
		 * simulate the current node and return the winner
		 */
		unsigned simulate(std::default_random_engine& engine) {
			board rollout = *this;
			std::vector<int> moves = all_moves(engine);
			while (std::find_if(moves.begin(), moves.end(),
					[&](int move) { return rollout.place(move) == board::legal; }) != moves.end());
			return (rollout.info().who_take_turns == board::white) ? board::black : board::white;
		}

		/**
		 * update statistics for all nodes saved in the path
		 */
		void update(std::vector<node*>& path, unsigned winner, 
			std::unordered_map<board::piece_type, std::unordered_map<int, int>> &rave_win,
			std::unordered_map<board::piece_type, std::unordered_map<int, int>> &rave_visit) {
			for (node* ndptr : path) {
				ndptr->win += (winner == info().who_take_turns) ? 1 : 0;
				ndptr->visit += 1;
				update_rave(ndptr, winner, rave_win, rave_visit);
			}
		}

		void update_rave(node* ndptr, int winner,
				std::unordered_map<board::piece_type, std::unordered_map<int, int>> &rave_win,
				std::unordered_map<board::piece_type, std::unordered_map<int, int>> &rave_visit) {
			// update rave value
			board::piece_type color = ndptr.info().who_take_turns;
			int move = ndptr.info().last_move_index;
			if (rave_win.count(who) == 0) rave_win[color] = std::unordered_map<size_t, float>();
			if (rave_win[color].count(move) == 0) rave_win[color][move] = 0;
			if (winner == color) {
				rave_win[color][move]++;
			}
			rave_visit[color][move]++;
		}

		/**
		 * pick the best action by visit counts
		 */
		action take_action() const {
			auto best = std::max_element(child.begin(), child.end(),
					[](const node& lhs, const node& rhs) { return lhs.visit < rhs.visit; });
			if (best == child.end()) return action(); // no legal move
			return action::place(best->info().last_move_index, info().who_take_turns);
		}

	private:

		/**
		 * check whether this node is a fully-expanded non-terminal node
		 */
		bool is_selectable() const {
			size_t num_moves = 0;
			int move = 0;
			while(move < 81){
				if (board(*this).place(move) == board::legal)
					num_moves++;
				move++;
			}
			return child.size() == num_moves && num_moves > 0;
		}

		/**
		 * get the ucb score of this node
		 */
		float ucb_score(float c = std::sqrt(2),
			std::unordered_map<board::piece_type, std::unordered_map<int, int>> rave_win,
			std::unordered_map<board::piece_type, std::unordered_map<int, int>> rave_visit) const {
			board::piece_type color = info().who_take_turns;
			int move = info().last_move_index;
			float numerator = float(rave_win[color][move]) + float(win) + std::sqrt(log_visits_ * child.visits_) * c;
			float denominator = visit + rave_visit[color][move];
			//float exploit = float(win) / visit;
			//float explore = std::sqrt(std::log(parent->visit) / visit);
			return numerator / denominator;
		}

		/**
		 * get all moves in shuffled order
		 */
		std::vector<int> all_moves(std::default_random_engine& engine) const {
			std::vector<int> moves;
			int move = 0;
			while(move < 81){
				moves.push_back(move);
				move++;
			}	
			std::shuffle(moves.begin(), moves.end(), engine);
			return moves;
		}

	private:
		size_t win, visit, rave_win, rave_visit;
		std::vector<node> child;
		node* parent;
	};
private:

};
