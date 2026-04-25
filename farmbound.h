#ifndef FARMBOUND_H
#define FARMBOUND_H

#include <stdint.h>

#define COL_WATER 1
#define COL_MANURE 2
#define COL_FERTILISER 3

#define EMPTY -1
#define SEED 0
#define CROP 1
#define FIELD 2
#define SCYTHE 3
#define HARVESTER 4
#define WATER 5
#define MANURE 6
#define FERTILISER 7
#define LEFT 8
#define RIGHT 9
#define UP 10
#define DOWN 11

struct farm_item{
	int e;
	int evolve;
	char icon[6];
	double l;
	int sort;
};

extern const struct farm_item g_items[12];

struct board_space{
	struct board_space *next, *prev;
	struct board_space *l;
	struct board_space *r;
	struct board_space *u;
	struct board_space *d;
	int g;
	int click;
	int pos;
	int e;
	int e_old;
};

struct boost_event{
	struct boost_event *next, *prev;
	struct board_space *b;
	struct board_space *booster;
	int boost_to;
	int reduce_booster_to;
};

struct game_data{
	struct board_space board[16];
	struct board_space *groups[16];
	struct boost_event *boosts;
	int boost_used[16];
	int background_colour[16];
	int score_colour[16];
	int total_score;
	int CLICKS;
	int move_count;
	int CURRENT;
	int32_t seed_main;
};

void fb__init_game(struct game_data *gd);
void fb__coalesce(struct game_data *gd);
void fb__clear_groups(struct game_data *gd);
bool fb__handle_click(struct game_data *gd, char keypress);
bool fb__next_item(struct game_data *gd);
void fb__print_board(struct game_data *gd);
void fb__set_seed(struct game_data *gd, char *custom_seed);

#endif
