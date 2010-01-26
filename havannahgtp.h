
#ifndef _HAVANNAHGTP_H_
#define _HAVANNAHGTP_H_

#include "gtp.h"
#include "game.h"
#include "string.h"
#include "solver.h"

class HavannahGTP : public GTPclient {
	HavannahGame game;

public:
	bool verbose;
	bool hguicoords;

	HavannahGTP(FILE * i = stdin, FILE * o = stdout){
		GTPclient(i, o);

		verbose = false;
		hguicoords = false;

		newcallback("name",            bind(&HavannahGTP::gtp_name,       this, _1));
		newcallback("version",         bind(&HavannahGTP::gtp_version,    this, _1));
		newcallback("verbose",         bind(&HavannahGTP::gtp_verbose,    this, _1));
		newcallback("debug",           bind(&HavannahGTP::gtp_debug,      this, _1));
		newcallback("hguicoords",      bind(&HavannahGTP::gtp_hguicoords, this, _1));
		newcallback("gridcoords",      bind(&HavannahGTP::gtp_gridcoords, this, _1));
		newcallback("print",           bind(&HavannahGTP::gtp_print,      this, _1));
		newcallback("showboard",       bind(&HavannahGTP::gtp_print,      this, _1));
		newcallback("clear_board",     bind(&HavannahGTP::gtp_clearboard, this, _1));
		newcallback("boardsize",       bind(&HavannahGTP::gtp_boardsize,  this, _1));
		newcallback("play",            bind(&HavannahGTP::gtp_play,       this, _1));
		newcallback("white",           bind(&HavannahGTP::gtp_playwhite,  this, _1));
		newcallback("black",           bind(&HavannahGTP::gtp_playblack,  this, _1));
		newcallback("undo",            bind(&HavannahGTP::gtp_undo,       this, _1));
		newcallback("havannah_winner", bind(&HavannahGTP::gtp_winner,     this, _1));
		newcallback("havannah_solve",  bind(&HavannahGTP::gtp_solve,      this, _1));
	}

	GTPResponse gtp_print(vecstr args){
		return GTPResponse(true, "\n" + game.getboard()->to_s());
	}

	string won_str(int outcome) const {
		switch(outcome){
			case -1: return "none";
			case 0:  return "tie";
			case 1:  return "white";
			case 2:  return "black";
		}
	}

	GTPResponse gtp_boardsize(vecstr args){
		if(args.size() != 1)
			return GTPResponse(false, "Wrong number of arguments");

		int size = atoi(args[0].c_str());
		if(size < 3 || size > 10)
			return GTPResponse(false, "Size " + to_str(size) + " is out of range.");

		game = HavannahGame(size);
		return GTPResponse(true);
	}

	GTPResponse gtp_clearboard(vecstr args){
		game.clear();
		return GTPResponse(true);
	}

	GTPResponse gtp_undo(vecstr args){
		game.undo();
		if(verbose)
			return GTPResponse(true, "\n" + game.getboard()->to_s());
		else
			return GTPResponse(true);
	}

	GTPResponse gtp_solve(vecstr args){
		if(args.size() != 1)
			return GTPResponse(false, "Wrong number of arguments");

		Solver solve(*(game.getboard()), atof(args[0].c_str()));

		string ret = "";
		ret += won_str(solve.outcome) + " ";
		ret += move_str(solve.x, solve.y) + " ";
		ret += to_str(solve.maxdepth) + " ";
		ret += to_str(solve.nodes);
		return GTPResponse(true, ret);
	}

	void parse_move(const string & str, int & x, int & y){
		x = tolower(str[0]) - 'a';
		y = atoi(str.c_str() + 1) - 1;

		if(!hguicoords && x >= game.getsize())
			y += x + 1 - game.getsize();
	}

	string move_str(int x, int y){
		if(!hguicoords && x >= game.getsize())
			y -= x + 1 - game.getsize();

		return string() + char(x + 'a') + to_str(y+1);
	}

	GTPResponse play(const string & pos, int toplay){
		int x, y;
		parse_move(pos, x, y);
		if(!game.getboard()->onboard2(x, y))
			return GTPResponse(false, "Move out of bounds");

		if(!game.move(x, y, toplay))
			return GTPResponse(false, "Invalid placement, already taken");

		if(verbose)
			return GTPResponse(true, "Placement: " + move_str(x, y) + ", outcome: " + game.getboard()->won_str() + "\n" + game.getboard()->to_s());
		else
			return GTPResponse(true);
	}

	GTPResponse gtp_play(vecstr args){
		if(args.size() != 2)
			return GTPResponse(false, "Wrong number of arguments");

		char toplay = 0;
		switch(tolower(args[0][0])){
			case 'w': toplay = 1; break;
			case 'b': toplay = 2; break;
			default:
				return GTPResponse(false, "Invalid player selection");
		}

		return play(args[1], toplay);
	}

	GTPResponse gtp_playwhite(vecstr args){
		if(args.size() != 1)
			return GTPResponse(false, "Wrong number of arguments");

		return play(args[0], 1);
	}

	GTPResponse gtp_playblack(vecstr args){
		if(args.size() != 1)
			return GTPResponse(false, "Wrong number of arguments");

		return play(args[0], 2);
	}

	GTPResponse gtp_winner(vecstr args){
		return GTPResponse(true, game.getboard()->won_str());
	}

	GTPResponse gtp_name(vecstr args){
		return GTPResponse(true, "Castro");
	}

	GTPResponse gtp_version(vecstr args){
		return GTPResponse(true, "0.1");
	}

	GTPResponse gtp_verbose(vecstr args){
		verbose = true;
		return GTPResponse(true, "Verbose on");
	}

	GTPResponse gtp_hguicoords(vecstr args){
		hguicoords = true;
		return GTPResponse(true);
	}
	GTPResponse gtp_gridcoords(vecstr args){
		hguicoords = false;
		return GTPResponse(true);
	}


	GTPResponse gtp_debug(vecstr args){
		string str = "\n";
		str += "Board size:  " + to_str(game.getboard()->get_size()) + "\n";
		str += "Board cells: " + to_str(game.getboard()->numcells()) + "\n";
		str += "Board vec:   " + to_str(game.getboard()->vecsize()) + "\n";
		str += "Board mem:   " + to_str(game.getboard()->memsize()) + "\n";

		return GTPResponse(true, str);
	}
};

#endif
