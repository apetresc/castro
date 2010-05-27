
#include "player.h"
#include "board.h"
#include <cmath>
#include <string>
#include "string.h"

Move Player::mcts(double time, int maxruns, uint64_t memlimit){
	maxnodes = memlimit*1024*1024/sizeof(Node);
	time_used = 0;
	runs = 0;

	treelen.reset();
	gamelen.reset();

	if(rootboard.won() >= 0 || (time <= 0 && maxruns == 0))
		return Move(M_RESIGN);

	timeout = false;
	Timer timer;
	if(time > 0)
		timer.set(time, bind(&Player::timedout, this));

	int starttime = time_msec();


	double ptime = time * prooftime * rootboard.num_moves() / rootboard.numcells(); //ie scale up from 0 to prooftime
	if(ptime > 0.01){ //minimum of 10ms worth of solve time
		Timer timer2 = Timer(ptime, bind(&Solver::timedout, &solver));
		int ret = solver.run_pnsab(rootboard, (rootboard.toplay() == 1 ? 2 : 1), memlimit/2);

		//if it found a win, just play it
		if(ret == 1){
			int runtime = time_msec() - starttime;
			fprintf(stderr, "Solved in %i msec\n", runtime);
			time_used = (double)runtime/1000;
			return solver.bestmove;
		}

	//not sure how to transfer the knowledge in the proof tree to the UCT tree
	//the difficulty is taking into account keeping the tree between moves and adding knowledge heuristics to unproven nodes
//		nodes = root.construct(solver.root, proofscore);
		solver.reset();
	}


	root.outcome = -1;
	runs = 0;
	RaveMoveList movelist(rootboard.movesremain());
	do{
		root.exp += 0;
		runs++;
		Board copy = rootboard;
		movelist.clear();
		walk_tree(copy, & root, movelist, 0);
	}while(!timeout && root.outcome == -1 && (maxruns == 0 || runs < maxruns));

//return the best one
	Node * ret = return_move(& root, rootboard.toplay());

	int runtime = time_msec() - starttime;
	time_used = (double)runtime/1000;

	string stats = "Finished " + to_str(runs) + " runs in " + to_str(runtime) + " msec\n";
	stats += "Game length: " + gamelen.to_s() + "\n";
	stats += "Tree depth:  " + treelen.to_s() + "\n";
	stats += "Move Score:  " + to_str(ret->exp.avg()) + "\n";
	stats += "Games/s:     " + to_str((int)((double)runs*1000/runtime)) + "\n";
	if(ret->outcome >= 0){
		stats += "Solved as a ";
		if(ret->outcome == 0)                       stats += "draw";
		else if(ret->outcome == rootboard.toplay()) stats += "win";
		else                                        stats += "loss";
		stats += "\n";
	}
	fprintf(stderr, "%s", stats.c_str());

	return ret->move;
}

vector<Move> Player::get_pv(){
	vector<Move> pv;

	Node * n = & root;
	char turn = rootboard.toplay();
	while(!n->children.empty()){
		n = return_move(n, turn);
		pv.push_back(n->move);
		turn = 3 - turn;
	}

	if(pv.size() == 0)
		pv.push_back(Move(M_RESIGN));

	return pv;
}

Player::Node * Player::return_move(const Node * node, int toplay) const {
	Node * ret;
	ret = return_move_outcome(node, toplay);     if(ret) return ret; //win
	ret = return_move_outcome(node, -1);         if(ret) return ret; //unknown
	ret = return_move_outcome(node, 0);          if(ret) return ret; //tie
	ret = return_move_outcome(node, 3 - toplay); if(ret) return ret; //lose

	assert(ret);
	return NULL;
}

Player::Node * Player::return_move_outcome(const Node * node, int outcome) const {
	int val, maxval = -1000000000;

	Node * ret = NULL,
		 * child = node->children.begin(),
		 * end = node->children.end();

	for( ; child != end; child++){

		if(child->outcome != outcome)
			continue;

		val = child->exp.num();
//		val = child->exp.avg();

		if(maxval < val){
			maxval = val;
			ret = child;
		}
	}

	return ret;
}


//return the winner of the simulation
int Player::walk_tree(Board & board, Node * node, RaveMoveList & movelist, int depth){
	int toplay = board.toplay();

	if(!node->children.empty() && node->outcome == -1){
	//choose a child and recurse
		Node * child = choose_move(node, toplay);

		if(child->outcome == -1){
			movelist.add(child->move, toplay);
			assert(board.move(child->move, locality));

			int won = walk_tree(board, child, movelist, depth+1);

			child->exp += (won == 0 ? 0.5 : won == toplay);

			if(child->outcome == toplay){ //backup a win right away. Losses and ties can wait
				node->outcome = child->outcome;
				node->bestmove = child->move;
				if(!minimaxtree && node != &root)
					nodes -= node->dealloc();
			}else if(ravefactor > min_rave){ //update the rave scores
				update_rave(node, movelist, won, toplay);
			}

			return won;
		}else{ //backup the win/loss/tie
			node->outcome = child->outcome;
			node->bestmove = child->move;
			if(!minimaxtree && node != &root)
				nodes -= node->dealloc();
		}
	}

	int won = (minimax ? node->outcome : board.won());
	if(won >= 0 || node->exp.num() < visitexpand || nodes >= maxnodes){
	//do random game on this node, unless it's already the end
		if(won == -1)
			won = rand_game(board, movelist, node->move, depth);

		treelen.add(depth);

		if(ravefactor > min_rave){
			if(won == 0 || (shortrave && movelist.size() > gamelen.avg()))
				movelist.clear();
			else
				movelist.clean(ravescale);
		}

		return won;
	}

//create children
	nodes += node->alloc(board.movesremain());

	int unknown = 0;

	Node * child = node->children.begin(),
	     * end   = node->children.end();
	Board::MoveIterator move = board.moveit();
	for(; !move.done() && child != end; ++move, ++child){
		*child = Node(*move);

		if(minimax){
			if(minimax == 1){
				child->outcome = board.test_win(*move);
			}else{
				Board next = board;
				next.move(*move);

				int abval = solver.negamax(next, minimax-1, -2, 2);

				switch(abval+2){
					case 0: /* -2 */ child->outcome = toplay;     break;
					case 1: /* -1 */ child->outcome = 0;          break;
					case 2: /*  0 */ child->outcome = -1;         break;
					case 3: /*  1 */ child->outcome = 0;          break;
					case 4: /*  2 */ child->outcome = 3 - toplay; break;
					default:
						assert(abval >= -2 && abval <= 2);
				}
			}

			if(child->outcome == toplay){ //proven win from here, don't need children
				node->outcome = child->outcome;
				node->bestmove = *move;
				unknown = 123456; //set to something big to skip the part below
				if(node != &root){
					nodes -= node->dealloc();
					break;
				}
			}

			if(child->outcome == -1)
				unknown++;
		}

		add_knowledge(board, node, child);
	}
	//both end conditions should happen in parallel, so either both happen or neither do
	assert(move.done() == (child == end));

	//Add experience to the one available move so the current simulation continues past this move
	if(unknown == 1){
		for(Node * child = node->children.begin(); child != node->children.end(); ++child){
			if(child->outcome == -1){
				child->exp.addwins(visitexpand);
				break;
			}
		}
	}

	return walk_tree(board, node, movelist, depth);
}

Player::Node * Player::choose_move(const Node * node, int toplay) const {
	float val, maxval = -1000000000;
	float logvisits = log(node->exp.num());

	float raveval = ravefactor*(skiprave == 0 || rand() % skiprave > 0); // = 0 or ravefactor

	Node * ret = NULL,
		 * child = node->children.begin(),
		 * end = node->children.end();

	for(; child != end; child++){
		if(child->outcome >= 0){
			if(child->outcome == toplay) //return a win immediately
				return child;

			val = (child->outcome == 0 ? -1 : -2); //-1 for tie so any unknown is better, -2 for loss so it's even worse
		}else{
			val = child->value(raveval, fpurgency) + explore*sqrt(logvisits/(child->exp.num() + 1));
		}

		if(maxval < val){
			maxval = val;
			ret = child;
		}
	}

	return ret;
}

void Player::update_rave(const Node * node, const RaveMoveList & movelist, int won, int toplay){
	//update the rave score of all children that were played
	RaveMoveList::iterator rave = movelist.begin(), raveend = movelist.end();
	Node * child = node->children.begin(),
	     * childend = node->children.end();

	while(rave != raveend && child != childend){
		if(*rave == child->move){
			if(rave->player == toplay || opmoves)
				child->rave += (rave->player == won ? rave->score : 0);
			rave++;
			child++;
		}else if(*rave > child->move){
			child++;
		}else{ //(*rave < child->move)
			rave++;
		}
	}
}

void Player::add_knowledge(Board & board, Node * node, Node * child){
	if(localreply){ //give exp boost for moves near the previous move
		int dist = node->move.dist(child->move);
		if(dist < 4)
			child->know.addwins(4 - dist);
	}

	if(locality) //give exp boost for moves near previous stones
		child->know.addwins(board.local(child->move));

	if(connect) //boost for moves that connect to edges/corners
		child->know.addwins(board.test_connectivity(child->move));

	if(bridge && test_bridge_probe(board, node->move, child->move))
		child->know.addwins(5);
}


//return a list of positions where the opponent is probing your virtual connections
bool Player::test_bridge_probe(const Board & board, const Move & move, const Move & test){
	if(move.dist(test) != 1)
		return false;

	bool equals = false;

	int state = 0;
	int piece = 3 - board.get(move);
	for(int i = 0; i < 8; i++){
		Move cur = move + neighbours[i % 6];

		bool on = board.onboard(cur);
		int v;
		if(on)
			v = board.get(cur);

	//state machine that progresses when it see the pattern, but counting borders as part of the pattern
		if(state == 0){
			if(!on || v == piece)
				state = 1;
			//else state = 0;
		}else if(state == 1){
			if(on){
				if(v == 0){
					state = 2;
					equals = (test == cur);
				}else if(v != piece)
					state = 0;
				//else (v==piece) => state = 1;
			}
			//else state = 1;
		}else{ // state == 2
			if(!on || v == piece){
				if(equals)
					return true;
				state = 1;
			}else{
				state = 0;
			}
		}
	}
	return false;
}


//look for good forced moves. In this case I only look for keeping a virtual connection active
//so looking from the last played position's perspective, which is a move by the opponent
//if you see a pattern of mine, empty, mine in the circle around the last move, their move
//would break the virtual connection, so should be played
//a virtual connection to a wall is also important
bool Player::check_pattern(const Board & board, Move & move){
	Move ret;
	int state = 0;
	int a = rand() % 6;
	int piece = 3 - board.get(move);
	for(int i = 0; i < 8; i++){
		Move cur = move + neighbours[(i+a)%6];

		bool on = board.onboard(cur);
		int v;
		if(on)
			v = board.get(cur);

/*
	//state machine that progresses when it see the pattern, but only taking pieces into account
		if(state == 0){
			if(on && v == piece)
				state = 1;
			//else state = 0;
		}else if(state == 1){
			if(on){
				if(v == 0){
					state = 2;
					ret = cur;
				}else if(v != piece)
					state = 0;
				//else (v==piece) => state = 1;
			}else
				state = 0;
		}else{ // state == 2
			if(on && v == piece){
				move = ret;
				return true;
			}else{
				state = 0;
			}
		}

/*/
	//state machine that progresses when it see the pattern, but counting borders as part of the pattern
		if(state == 0){
			if(!on || v == piece)
				state = 1;
			//else state = 0;
		}else if(state == 1){
			if(on){
				if(v == 0){
					state = 2;
					ret = cur;
				}else if(v != piece)
					state = 0;
				//else (v==piece) => state = 1;
			}
			//else state = 1;
		}else{ // state == 2
			if(!on || v == piece){
				move = ret;
				return true;
			}else{
				state = 0;
			}
		}
//*/
	}
	return false;
}

//play a random game starting from a board state, and return the results of who won	
int Player::rand_game(Board & board, RaveMoveList & movelist, Move move, int depth){
	int won;

	Move order[board.movesremain()];

	int i = 0;
	for(Board::MoveIterator m = board.moveit(); !m.done(); ++m)
		order[i++] = *m;

	random_shuffle(order, order + i);

	i = 0;
	while((won = board.won()) < 0){
		if(instantwin){
			for(Board::MoveIterator m = board.moveit(); !m.done(); ++m){
				if(board.test_win(*m) > 0){
					move = *m;
					goto makemove; //yes, evil...
				}
			}
		}

		if(rolloutpattern && check_pattern(board, move))
			goto makemove;

	//default...
		do{
			move = order[i++];
		}while(!board.valid_move(move));

makemove:
		movelist.add(move, board.toplay());
		board.move(move);
		depth++;
	}

	gamelen.add(depth);

	return won;
}


