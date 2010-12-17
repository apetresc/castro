
#include "player.h"
#include "board.h"
#include <cmath>
#include <string>
#include "string.h"
#include "solverab.h"
#include "timer.h"

void Player::PlayerThread::run(){
	while(!cancelled){
		switch(player->threadstate){
		case Thread_Cancelled:  //threads should exit
			return;

		case Thread_Wait_Start: //threads are waiting to start
			player->runbarrier.wait();
			CAS(player->threadstate, Thread_Wait_Start, Thread_Running);
			break;

		case Thread_Wait_End:   //threads are waiting to end
			player->runbarrier.wait();
			CAS(player->threadstate, Thread_Wait_End, Thread_Wait_Start);
			break;

		case Thread_Running:    //threads are running
			if(player->root.outcome >= 0 || (maxruns > 0 && runs >= maxruns)){ //solved or finished runs
				CAS(player->threadstate, Thread_Running, Thread_Wait_End);
				break;
			}
			if(player->ctmem.memused() >= player->maxmem){ //out of memory, start garbage collection
				CAS(player->threadstate, Thread_Running, Thread_GC);
				break;
			}

			runs++;
			iterate();
			break;

		case Thread_GC:         //one thread is running garbage collection, the rest are waiting
		case Thread_GC_End:     //once done garbage collecting, go to wait_end instead of back to running
			if(player->gcbarrier.wait()){
				logerr("Starting player GC with limit " + to_str(player->gclimit) + " ... ");
				uint64_t nodesbefore = player->nodes;
				Board copy = player->rootboard;
				player->garbage_collect(copy, & player->root, player->gclimit);
				player->flushlog();
				player->ctmem.compact();
				logerr(to_str(100.0*player->nodes/nodesbefore, 1) + " % of tree remains\n");

				if(player->ctmem.memused() >= player->maxmem/2)
					player->gclimit = (int)(player->gclimit*1.3);
				else if(player->gclimit > 5)
					player->gclimit = (int)(player->gclimit*0.9); //slowly decay to a minimum of 5

				CAS(player->threadstate, Thread_GC,     Thread_Running);
				CAS(player->threadstate, Thread_GC_End, Thread_Wait_End);
			}
			player->gcbarrier.wait();
			break;
		}
	}
}

Player::Node * Player::genmove(double time, int maxruns){
	time_used = 0;
	int toplay = rootboard.toplay();

	if(rootboard.won() >= 0 || (time <= 0 && maxruns == 0))
		return NULL;

	Time starttime;

	Timer timer;
	if(time > 0)
		timer.set(time, bind(&Player::timedout, this));

	int runs = 0;
	for(unsigned int i = 0; i < threads.size(); i++){
		runs += threads[i]->runs;
		threads[i]->reset();
		threads[i]->maxruns = maxruns;
	}
	if(runs)
		logerr("Pondered " + to_str(runs) + " runs\n");

	//let them run!
	if(threadstate == Thread_Wait_Start || threadstate == Thread_Wait_End){
		threadstate = Thread_Running;
		runbarrier.wait();
	}

	//threadstate == Thread_Running

	//wait for the timer to stop them
	runbarrier.wait();

	//threadstate == Thread_Wait_Start

	if(ponder && root.outcome < 0)
		runbarrier.wait();

	time_used = Time() - starttime;

//return the best one
	return return_move(& root, toplay);
}

vector<Move> Player::get_pv(){
	vector<Move> pv;

	Node * r, * n = & root;
	char turn = rootboard.toplay();
	while(!n->children.empty()){
		r = return_move(n, turn);
		if(!r) break;
		pv.push_back(r->move);
		turn = 3 - turn;
		n = r;
	}

	if(pv.size() == 0)
		pv.push_back(Move(M_RESIGN));

	return pv;
}

Player::Node * Player::return_move(Node * node, int toplay) const {
	double val, maxval = -1000000000000.0; //1 trillion

	Node * ret = NULL,
		 * child = node->children.begin(),
		 * end = node->children.end();

	for( ; child != end; child++){
		if(child->outcome >= 0){
			if(child->outcome == toplay) val =  800000000000.0 - child->exp.num(); //shortest win
			else if(child->outcome == 0) val = -400000000000.0 + child->exp.num(); //longest tie
			else                         val = -800000000000.0 + child->exp.num(); //longest loss
		}else{ //not proven
			if(msrave == -1) //num simulations
				val = child->exp.num();
			else if(msrave == -2) //num wins
				val = child->exp.sum();
			else
				val = child->value(msrave, 0, 0) - msexplore*sqrt(log(node->exp.num())/(child->exp.num() + 1));
		}

		if(maxval < val){
			maxval = val;
			ret = child;
		}
	}

//set bestmove, but don't touch outcome, if it's solved that will already be set, otherwise it shouldn't be set
	if(ret){
		node->bestmove = ret->move;
	}else if(node->bestmove == M_UNKNOWN){
		SolverAB solver;
		solver.set_board(rootboard);
		solver.solve(0.1);
		node->bestmove = solver.bestmove;
	}

	assert(node->bestmove != M_UNKNOWN);

	return ret;
}

void Player::garbage_collect(Board & board, Node * node, unsigned int limit){
	Node * child = node->children.begin(),
		 * end = node->children.end();

	for( ; child != end; child++){
		if(child->outcome >= 0){ //solved
			if(solved_logfile && child->exp.num() > 1000){ //log heavy solved nodes
				board.set(child->move);
				if(child->children.num() > 0)
					garbage_collect(board, child, limit);
				logsolved(board.gethash(), child);
				board.unset(child->move);
			}
			nodes -= child->dealloc(ctmem);
		}else if(child->exp.num() < limit){ //low exp, ignore solvedness since it's trivial to re-solve
			nodes -= child->dealloc(ctmem);
		}else if(child->children.num() > 0){
			board.set(child->move);
			garbage_collect(board, child, limit);
			board.unset(child->move);
		}
	}
}

