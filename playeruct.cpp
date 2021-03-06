
#include "player.h"
#include <cmath>
#include <string>
#include "string.h"

void Player::PlayerUCT::iterate(){
	if(player->profile){
		timestamps[0] = Time();
		stage = 0;
	}

	movelist.reset(&(player->rootboard));
	player->root.exp.addvloss();
	Board copy = player->rootboard;
	use_rave    = (unitrand() < player->userave);
	use_explore = (unitrand() < player->useexplore);
	walk_tree(copy, & player->root, 0);
	player->root.exp.addv(movelist.getexp(3-player->rootboard.toplay()));

	if(player->profile){
		times[0] += timestamps[1] - timestamps[0];
		times[1] += timestamps[2] - timestamps[1];
		times[2] += timestamps[3] - timestamps[2];
		times[3] += Time() - timestamps[3];
	}
}

void Player::PlayerUCT::walk_tree(Board & board, Node * node, int depth){
	int toplay = board.toplay();

	if(!node->children.empty() && node->outcome < 0){
	//choose a child and recurse
		Node * child;
		do{
			int remain = board.movesremain();
			child = choose_move(node, toplay, remain);

			if(child->outcome < 0){
				movelist.addtree(child->move, toplay);

				if(!board.move(child->move, (player->minimax == 0), (player->locality || player->weightedrandom) )){
					logerr("move failed: " + child->move.to_s() + "\n" + board.to_s(false));
					assert(false && "move failed");
				}

				child->exp.addvloss(); //balanced out after rollouts

				walk_tree(board, child, depth+1);

				child->exp.addv(movelist.getexp(toplay));

				if(!player->do_backup(node, child, toplay) && //not solved
					player->ravefactor > min_rave &&  //using rave
					node->children.num() > 1 &&       //not a macro move
					50*remain*(player->ravefactor + player->decrrave*remain) > node->exp.num()) //rave is still significant
					update_rave(node, toplay);

				return;
			}
		}while(!player->do_backup(node, child, toplay));

		return;
	}

	if(player->profile && stage == 0){
		stage = 1;
		timestamps[1] = Time();
	}

	int won = (player->minimax ? node->outcome : board.won());

	//if it's not already decided
	if(won < 0){
		//create children if valid
		if(node->exp.num() >= player->visitexpand+1 && create_children(board, node, toplay)){
			walk_tree(board, node, depth);
			return;
		}

		if(player->profile){
			stage = 2;
			timestamps[2] = Time();
		}

		//do random game on this node
		for(int i = 0; i < player->rollouts; i++){
			Board copy = board;
			rollout(copy, node->move, depth);
		}
	}else{
		movelist.finishrollout(won); //got to a terminal state, it's worth recording
	}

	treelen.add(depth);

	movelist.subvlosses(1);

	if(player->profile){
		timestamps[3] = Time();
		if(stage == 1)
			timestamps[2] = timestamps[3];
		stage = 3;
	}

	return;
}

bool sort_node_know(const Player::Node & a, const Player::Node & b){
	return (a.know > b.know);
}

bool Player::PlayerUCT::create_children(Board & board, Node * node, int toplay){
	if(!node->children.lock())
		return false;

	if(player->dists || player->detectdraw){
		dists.run(&board, (player->dists > 0), (player->detectdraw ? 0 : toplay));

		if(player->detectdraw){
//			assert(node->outcome == -3);
			node->outcome = dists.isdraw(); //could be winnable by only one side

			if(node->outcome == 0){ //proven draw, neither side can influence the outcome
				node->bestmove = *(board.moveit()); //just choose the first move since all are equal at this point
				node->children.unlock();
				return true;
			}
		}
	}

	CompactTree<Node>::Children temp;
	temp.alloc(board.movesremain(), player->ctmem);

	int losses = 0;

	Node * child = temp.begin(),
	     * end   = temp.end(),
	     * loss  = NULL;
	Board::MoveIterator move = board.moveit(player->prunesymmetry);
	int nummoves = 0;
	for(; !move.done() && child != end; ++move, ++child){
		*child = Node(*move);

		if(player->minimax){
			child->outcome = board.test_win(*move);

			if(player->minimax >= 2 && board.test_win(*move, 3 - board.toplay()) > 0){
				losses++;
				loss = child;
			}

			if(child->outcome == toplay){ //proven win from here, don't need children
				node->outcome = child->outcome;
				node->proofdepth = 1;
				node->bestmove = *move;
				node->children.unlock();
				temp.dealloc(player->ctmem);
				return true;
			}
		}

		if(player->knowledge)
			add_knowledge(board, node, child);
		nummoves++;
	}

	if(player->prunesymmetry)
		temp.shrink(nummoves); //shrink the node to ignore the extra moves
	else //both end conditions should happen in parallel
		assert(move.done() && child == end);

	//Make a macro move, add experience to the move so the current simulation continues past this move
	if(losses == 1){
		Node macro = *loss;
		temp.dealloc(player->ctmem);
		temp.alloc(1, player->ctmem);
		macro.exp.addwins(player->visitexpand);
		*(temp.begin()) = macro;
	}else if(losses >= 2){ //proven loss, but at least try to block one of them
		node->outcome = 3 - toplay;
		node->proofdepth = 2;
		node->bestmove = loss->move;
		node->children.unlock();
		temp.dealloc(player->ctmem);
		return true;
	}

	if(player->dynwiden > 0) //sort in decreasing order by knowledge
		sort(temp.begin(), temp.end(), sort_node_know);

	PLUS(player->nodes, temp.num());
	node->children.swap(temp);
	assert(temp.unlock());

	return true;
}

Player::Node * Player::PlayerUCT::choose_move(const Node * node, int toplay, int remain) const {
	float val, maxval = -1000000000;
	float logvisits = log(node->exp.num());
	int dynwidenlim = (player->dynwiden > 1.0 ? (int)(logvisits/player->logdynwiden)+2 : 361);

	float raveval = use_rave * (player->ravefactor + player->decrrave*remain);
	float explore = use_explore * player->explore;
	if(player->parentexplore)
		explore *= node->exp.avg();

	Node * ret = NULL,
		 * child = node->children.begin(),
		 * end   = node->children.end();

	for(; child != end && dynwidenlim >= 0; child++){
		if(child->outcome >= 0){
			if(child->outcome == toplay) //return a win immediately
				return child;

			val = (child->outcome == 0 ? -1 : -2); //-1 for tie so any unknown is better, -2 for loss so it's even worse
		}else{
			val = child->value(raveval, player->knowledge, player->fpurgency);
			if(explore > 0)
				val += explore*sqrt(logvisits/(child->exp.num() + 1));
			dynwidenlim--;
		}

		if(maxval < val){
			maxval = val;
			ret = child;
		}
	}

	return ret;
}

/*
backup in this order:

6 win
5 win/draw
4 draw if draw/loss
3 win/draw/loss
2 draw
1 draw/loss
0 lose
return true if fully solved, false if it's unknown or partially unknown
*/
bool Player::do_backup(Node * node, Node * backup, int toplay){
	int nodeoutcome = node->outcome;
	if(nodeoutcome >= 0) //already proven, probably by a different thread
		return true;

	if(backup->outcome == -3) //nothing proven by this child, so no chance
		return false;


	uint8_t proofdepth = backup->proofdepth;
	if(backup->outcome != toplay){
		uint64_t sims = 0, bestsims = 0, outcome = 0, bestoutcome = 0;
		backup = NULL;

		Node * child = node->children.begin(),
			 * end = node->children.end();

		for( ; child != end; child++){
			int childoutcome = child->outcome; //save a copy to avoid race conditions

			if(proofdepth < child->proofdepth+1)
				proofdepth = child->proofdepth+1;

			//these should be sorted in likelyness of matching, most likely first
			if(childoutcome == -3){ // win/draw/loss
				outcome = 3;
			}else if(childoutcome == toplay){ //win
				backup = child;
				outcome = 6;
				proofdepth = child->proofdepth+1;
				break;
			}else if(childoutcome == 3-toplay){ //loss
				outcome = 0;
			}else if(childoutcome == 0){ //draw
				if(nodeoutcome == toplay-3) //draw/loss
					outcome = 4;
				else
					outcome = 2;
			}else if(childoutcome == -toplay){ //win/draw
				outcome = 5;
			}else if(childoutcome == toplay-3){ //draw/loss
				outcome = 1;
			}else{
				logerr("childoutcome == " + to_str(childoutcome) + "\n");
				assert(false && "How'd I get here? All outcomes should be tested above");
			}

			sims = child->exp.num();
			if(bestoutcome < outcome){ //better outcome is always preferable
				bestoutcome = outcome;
				bestsims = sims;
				backup = child;
			}else if(bestoutcome == outcome && ((outcome == 0 && bestsims < sims) || bestsims > sims)){
				//find long losses or easy wins/draws
				bestsims = sims;
				backup = child;
			}
		}

		if(bestoutcome == 3) //no win, but found an unknown
			return false;
	}

	if(CAS(node->outcome, nodeoutcome, backup->outcome)){
		node->bestmove = backup->move;
		node->proofdepth = proofdepth;
	}else //if it was in a race, try again, might promote a partial solve to full solve
		return do_backup(node, backup, toplay);

	return (node->outcome >= 0);
}

//update the rave score of all children that were played
void Player::PlayerUCT::update_rave(const Node * node, int toplay){
	Node * child = node->children.begin(),
	     * childend = node->children.end();

	for( ; child != childend; ++child)
		child->rave.addv(movelist.getrave(toplay, child->move));
}

void Player::PlayerUCT::add_knowledge(Board & board, Node * node, Node * child){
	if(player->localreply){ //boost for moves near the previous move
		int dist = node->move.dist(child->move);
		if(dist < 4)
			child->know += player->localreply * (4 - dist);
	}

	if(player->locality) //boost for moves near previous stones
		child->know += player->locality * board.local(child->move, board.toplay());

	Board::Cell cell;
	if(player->connect || player->size)
		cell = board.test_cell(child->move);

	if(player->connect) //boost for moves that connect to edges/corners
		child->know += player->connect * (cell.numcorners() + cell.numedges());

	if(player->size) //boost for size of the group
		child->know += player->size * cell.size;

	if(player->bridge && test_bridge_probe(board, node->move, child->move)) //boost for maintaining a virtual connection
		child->know += player->bridge;

	if(player->dists)
		child->know += abs(player->dists) * max(0, board.get_size_d() - dists.get(child->move, board.toplay()));
}

//test whether this move is a forced reply to the opponent probing your virtual connections
bool Player::PlayerUCT::test_bridge_probe(const Board & board, const Move & move, const Move & test) const {
	if(move.dist(test) != 1)
		return false;

	bool equals = false;

	int state = 0;
	int piece = 3 - board.get(move);
	for(int i = 0; i < 8; i++){
		Move cur = move + neighbours[i % 6];

		bool on = board.onboard(cur);
		int v = 0;
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

///////////////////////////////////////////


//play a random game starting from a board state, and return the results of who won
int Player::PlayerUCT::rollout(Board & board, Move move, int depth){
	int won;
	int num = board.movesremain();

	bool wrand = (player->weightedrandom);

	if(wrand){
		wtree[0].resize(board.vecsize());
		wtree[1].resize(board.vecsize());

		int set = 0;
		for(Board::MoveIterator m = board.moveit(false, false); !m.done(); ++m){
			int i = board.xy(*m);
			moves[i] = *m;
			unsigned int p = board.pattern(i);
			wtree[0].set_weight_fast(i, player->gammas[p]);
			wtree[1].set_weight_fast(i, player->gammas[board.pattern_invert(p)]);
			set++;
		}

		wtree[0].rebuild_tree();
		wtree[1].rebuild_tree();
	}else{
		int i = 0;
		for(Board::MoveIterator m = board.moveit(false, false); !m.done(); ++m)
			moves[i++] = *m;

		i = num;
		while(i > 1){
			int j = rand32() % i--;
			Move tmp = moves[j];
			moves[j] = moves[i];
			moves[i] = tmp;
		}

//		random_shuffle(moves, moves + num);
	}

	int doinstwin = player->instwindepth;
	if(doinstwin < 0)
		doinstwin *= - board.get_size();

	bool checkrings = (unitrand() < player->checkrings);

	//only check rings to the specified depth
	int  checkdepth = (int)player->checkringdepth;
	//if it's negative, check for that fraction of the remaining moves
	if(player->checkringdepth < 0)
		checkdepth = (int)ceil(num * player->checkringdepth * -1);

	//only allow rings bigger than the minimum ring size, incrementing by the ringincr after each move
	int minringsize = (int)player->minringsize;
	int ringcounterfull = (int)player->ringincr;
	//if it's negative, scale by the fraction of remaining moves
	if(player->ringincr < 0)
		ringcounterfull = (int)ceil(num * player->ringincr * -1);

	int ringcounter = ringcounterfull;

	int ringperm = player->ringperm;

	Move * nextmove = moves;
	Move forced = M_UNKNOWN;
	while((won = board.won()) < 0){
		int turn = board.toplay();

		if(forced == M_UNKNOWN){
			//do a complex choice
			PairMove pair = rollout_choose_move(board, move, doinstwin, checkrings);
			move = pair.a;
			forced = pair.b;

			//or the simple random choice if complex found nothing
			if(move == M_UNKNOWN){
				do{
					if(wrand){
						int j = wtree[turn-1].choose();
//						assert(j >= 0);
						wtree[0].set_weight(j, 0);
						wtree[1].set_weight(j, 0);
						move = moves[j];
					}else{
						move = *nextmove;
						nextmove++;
					}
				}while(!board.valid_move_fast(move));
			}
		}else{
			move = forced;
			forced = M_UNKNOWN;
		}

		movelist.addrollout(move, turn);

		board.move(move, true, false, (checkrings ? minringsize : 0), ringperm);
		if(--ringcounter == 0){
			minringsize++;
			ringcounter = ringcounterfull;
		}
		depth++;
		checkrings &= (depth < checkdepth);

		if(wrand){
			//update neighbour weights
			for(const MoveValid * i = board.nb_begin(move), *e = board.nb_end(i); i < e; i++){
				if(i->onboard() && board.get(i->xy) == 0){
					unsigned int p = board.pattern(i->xy);
					wtree[0].set_weight(i->xy, player->gammas[p]);
					wtree[1].set_weight(i->xy, player->gammas[board.pattern_invert(p)]);
				}
			}
		}
	}

	gamelen.add(depth);

	if(won > 0)
		wintypes[won-1][(int)board.getwintype()].add(depth);

	//update the last good reply table
	if(player->lastgoodreply && won > 0){
		MoveList::RaveMove * rave = movelist.begin(), *raveend = movelist.end();

		int m = -1;
		while(rave != raveend){
			if(m >= 0){
				if(rave->player == won && *rave != M_SWAP)
					goodreply[rave->player - 1][m] = *rave;
				else if(player->lastgoodreply == 2)
					goodreply[rave->player - 1][m] = M_UNKNOWN;
			}
			m = board.xy(*rave);
			++rave;
		}
	}

	movelist.finishrollout(won);
	return won;
}

PairMove Player::PlayerUCT::rollout_choose_move(Board & board, const Move & prev, int & doinstwin, bool checkrings){
	//look for instant wins
	if(player->instantwin == 1 && --doinstwin >= 0){
		for(Board::MoveIterator m = board.moveit(); !m.done(); ++m)
			if(board.test_win(*m, board.toplay(), checkrings) > 0)
				return *m;
	}

	//look for instant wins and forced replies
	if(player->instantwin == 2 && --doinstwin >= 0){
		Move loss = M_UNKNOWN;
		for(Board::MoveIterator m = board.moveit(); !m.done(); ++m){
			if(board.test_win(*m, board.toplay(), checkrings) > 0) //win
				return *m;
			if(board.test_win(*m, 3 - board.toplay(), checkrings) > 0) //lose
				loss = *m;
		}
		if(loss != M_UNKNOWN)
			return loss;
	}

	if(player->instantwin >= 3 && --doinstwin >= 0){
		Move start, cur, loss = M_UNKNOWN;
		int turn = 3 - board.toplay();

		if(player->instantwin == 4){ //must have an edge or corner connection, or it has nothing to offer a group towards a win, ignores rings
			const Board::Cell * c = board.cell(prev);
			if(c->numcorners() == 0 && c->numedges() == 0)
				goto skipinstwin3;

		}

//		logerr(board.to_s(true));

		//find the first empty cell
		int dir = -1;
		for(int i = 0; i <= 5; i++){
			start = prev + neighbours[i];

			if(!board.onboard(start) || board.get(start) != turn){
				dir = (i + 5) % 6;
				break;
			}
		}

		if(dir == -1) //possible if it's in the middle of a ring, which is possible if rings are being ignored
			goto skipinstwin3;

		cur = start;

//		logerr(prev.to_s() + ":");

		//follow contour of the current group looking for wins
		do{
//			logerr(" " + to_str((int)cur.y) + "," + to_str((int)cur.x));
			//check the current cell
			if(board.onboard(cur) && board.get(cur) == 0 && board.test_win(cur, turn, checkrings) > 0){
//				logerr(" loss");
				if(loss == M_UNKNOWN)
					loss = cur;
				else if(loss != cur)
					return PairMove(loss, cur); //game over, two wins found for opponent
			}

			//advance to the next cell
			for(int i = 5; i <= 9; i++){
				int nd = (dir + i) % 6;
				Move next = cur + neighbours[nd];

				if(!board.onboard(next) || board.get(next) != turn){
					cur = next;
					dir = nd;
					break;
				}
			}
		}while(cur != start); //potentially skips part of it when the start is in a pocket, rare bug

//		logerr("\n");

		if(loss != M_UNKNOWN)
			return loss;
	}
skipinstwin3:

	//force a bridge reply
	if(player->rolloutpattern){
		Move move = rollout_pattern(board, prev);
		if(move != M_UNKNOWN)
			return move;
	}

	//reuse the last good reply
	if(player->lastgoodreply && prev != M_SWAP){
		Move move = goodreply[board.toplay()-1][board.xy(prev)];
		if(move != M_UNKNOWN && board.valid_move_fast(move))
			return move;
	}

	return M_UNKNOWN;
}

//look for good forced moves. In this case I only look for keeping a virtual connection active
//so looking from the last played position's perspective, which is a move by the opponent
//if you see a pattern of mine, empty, mine in the circle around the last move, their move
//would break the virtual connection, so should be played
//a virtual connection to a wall is also important
Move Player::PlayerUCT::rollout_pattern(const Board & board, const Move & move){
	Move ret;
	int state = 0;
	int a = (++rollout_pattern_offset % 6);
	int piece = 3 - board.get(move);
	for(int i = 0; i < 8; i++){
		Move cur = move + neighbours[(i+a)%6];

		bool on = board.onboard(cur);
		int v = 0;
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
					ret = cur;
				}else if(v != piece)
					state = 0;
				//else (v==piece) => state = 1;
			}
			//else state = 1;
		}else{ // state == 2
			if(!on || v == piece){
				return ret;
			}else{
				state = 0;
			}
		}
	}
	return M_UNKNOWN;
}
