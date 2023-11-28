#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <utlist.h>

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

const struct farm_item g_items[] = {
	{SEED, CROP, "🌱", 0.65, 0},
	{CROP, FIELD, "🌿", 0.1, 0},
	{FIELD, -1, "🌾", 0, 0},
	{SCYTHE, HARVESTER, "🔪", 0.1, 0},
	{HARVESTER, -1, "🚜", 0, 0},
	{WATER, MANURE, "🚰", 0.1, 0},
	{MANURE, FERTILISER, "💩", 0.01, 0},
	{FERTILISER, -1, "⚗", 0, 0},
	{LEFT, -1, "🡨", 0.01, 1},
	{RIGHT, -1, "🡪", 0.01, 2},
	{UP, -1, "🡡", 0.01, 3},
	{DOWN, -1, "🡫", 0.01, 0},
};

struct board_space{
	struct board_space *next, *prev;
	struct board_space *l;
	struct board_space *r;
	struct board_space *u;
	struct board_space *d;
	struct board_space *booster;
	int g;
	int click;
	int pos;
	int e;
};

struct boost_event{
	struct boost_event *next, *prev;
	struct board_space *b;
	struct board_space *booster;
	int boost_to;
	int reduce_booster_to;
};

static struct board_space board[16];
static struct board_space *groups[16];
static struct boost_event *boosts = NULL;
static int boost_used[16];
static int total_score;
static int CLICKS = 0;
static int move_count = 0;
static int CURRENT = EMPTY;
static int32_t seed_main;

/* 20/05/2023 - no arrows in the first 200 */
//const char moves[] = "MSSRSSKWSSSWSSSWKSSCCSSSKSKSSCSSCKCSWSSSSSSKSSSSSSSSSSCSSSSSSSSSKCSSSSSKSSSKSCSSCKCSSSCSSSSSSSWSSSSSCKMSWCSKSSSSSCSWSWSCSSSSKSSSSSKWSSSSSSSWCSSSSSSSSSSKSSSSSSSWSSSSSKSWWWSSSWCSSSKSSCWWSSWSSSSSSKSSKCWSS";

static void term_fix(void)
{
	struct termios ti;

	tcgetattr(fileno(stdin), &ti);
	ti.c_lflag &= ~(ECHO | ECHONL | ICANON);
	tcsetattr(fileno(stdin), 0, &ti);
}

static void term_restore(void)
{
	struct termios ti;

	tcgetattr(fileno(stdin), &ti);
	ti.c_lflag |= ECHO | ECHONL | ICANON;
	tcsetattr(fileno(stdin), 0, &ti);
}

static void clear_groups(void)
{
	memset(groups, 0, sizeof(groups));
	memset(&boost_used, 0, sizeof(boost_used));
	boosts = NULL;

	for(int i=0; i<16; i++){
		board[i].next = NULL;
		board[i].prev = NULL;
		board[i].g = 0;
	}
}

static int click_cmp(struct board_space *a, struct board_space *b)
{
	return b->click - a->click;
}

static int boost_cmp(struct boost_event *a, struct boost_event *b)
{
	return b->b->click - a->b->click;
}

static void print_board(void)
{
	printf("\e[2J");
	printf("\e[1,1H");
	for(int i=0; i<16; i++){
		if(board[i].e == EMPTY){
			//printf("⚫️");
			//printf("🟫");
			printf("⬛️");
		}else{
			printf("%s", g_items[board[i].e].icon);
		}
		if(i == 7){
			printf("  Next:  %s\n", g_items[CURRENT].icon);
		}else if(i == 11){
			printf("  Moves: %d\n", move_count-1);
		}else if(i == 15){
			printf("  Score: %d\n", total_score);
		}else if(i == 3){
			printf("\n");
		}
	}
}


static void evolve(struct board_space **group, int icon) {
	struct board_space *b;

	DL_FOREACH(*group, b){
		b->e = EMPTY;
	}
	(*group)->e = icon;
	*group = NULL;
}

static void coalesce(void)
{
	// collect into groups
	clear_groups();

	int groupcount = 1;
	for(int i=0; i<16; i++){
		struct board_space *b = &board[i];

		if (b->e == EMPTY) continue;

		if (b->u && b->u->e == b->e && b->l && b->l->e == b->e) {
			// this item matches on the left and above
			// so left and above are actually one group, but
			// currently they'll be two
			// so unify them
			b->g = b->u->g;
			// now find everything in the group with b->l and make it b->u's group
			int leftgroup = b->l->g;
			for(int j=0; j<16; j++){
				struct board_space *nb = &board[j];
				if (nb->g == leftgroup) nb->g = b->g;
			}
		} else if (b->u && b->u->e == b->e) { b->g = b->u->g; }
		else if (b->l && b->l->e == b->e) { b->g = b->l->g; }
		else { b->g = groupcount++; }
	}

	for(int i=0; i<16; i++){
		struct board_space *b = &board[i];
		b->next = NULL;
		b->prev = NULL;

		if(b->g){
			DL_APPEND(groups[b->g], b);
		}
	}
	bool changed = false;

	for(int i=1; i<16; i++){
		struct board_space *g = groups[i], *gtmp;
		int count;

		DL_COUNT(groups[i], gtmp, count);
		if(count < 3) continue;

		DL_SORT(groups[i], click_cmp);

		int evolve_icon = g_items[g->e].evolve;
		if (evolve_icon != -1) { // FIELD can't be evolved, for example
			evolve(&groups[i], evolve_icon);
			changed = true;
		}
	}
	if (changed){
		coalesce();
	}
}

static int score_add(struct board_space *b)
{
	if(b && b->e == SCYTHE) return 2;
	else if(b && b->e == HARVESTER) return 5;
	else return 0;
}


static void boost_add_event(struct board_space *b, struct board_space *booster, int boost_to, int reduce_booster_to)
{
	struct boost_event *ev = calloc(1, sizeof(struct boost_event));
	if(!ev) exit(1);

	ev->b = b;
	ev->booster = booster;
	ev->boost_to = g_items[b->e].evolve;
	ev->reduce_booster_to = reduce_booster_to;
	DL_APPEND(boosts, ev);
}

static void boost_add(struct board_space *b, struct board_space *other){
	if (!other || other->e == EMPTY) return;

	if (other->e == WATER && !boost_used[other->pos]) {
		// there's unused water next to this field, so use it
		boost_used[other->pos] = 1;
		boost_add_event(b, other, g_items[b->e].evolve, EMPTY);
		b->booster = other;
	} else if (other->e == MANURE && boost_used[other->pos] < 4) {
		boost_used[other->pos] += 1;
		boost_add_event(b, other, g_items[b->e].evolve, WATER);
		b->click = CLICKS++;
	} else if (other->e == FERTILISER && boost_used[other->pos] < 10) {
		boost_used[other->pos] += 1;
		boost_add_event(b, other, g_items[b->e].evolve, MANURE);
		b->click = CLICKS++;
	}
}

static void tick(void)
{
	int score = 0;
	memset(&boost_used, 0, sizeof(boost_used));

	boosts = NULL;
	for(int i=0; i<16; i++){
		board[i].next = NULL;
		board[i].prev = NULL;
	}

	for(int i=0; i<16; i++){
		struct board_space *b = &board[i];

		// point scoring via harvesting
		if (b->e == CROP || b->e == FIELD){
			score += score_add(b->u);
			score += score_add(b->d);
			score += score_add(b->l);
			score += score_add(b->r);
		}

		// boosts (fertiliser, manure, water)
		if(b->e == SEED || b->e == CROP){
			boost_add(b, b->l);
			boost_add(b, b->u);
			boost_add(b, b->d);
			boost_add(b, b->r);
		}
	}
	total_score += score;

	DL_SORT(boosts, boost_cmp);

	struct boost_event *ev, *ev_tmp;
	DL_FOREACH_SAFE(boosts, ev, ev_tmp){
		ev->booster->e = ev->reduce_booster_to;
		ev->b->e = ev->boost_to;
		DL_DELETE(boosts, ev);
		free(ev);
	}
}

static void end_game(void)
{
	print_board();
	exit(0);
}


uint32_t mullberry32(int32_t *x) {
	uint32_t z = ((*x) += 0x6D2B79F5UL);
	z = (z ^ (z >> 15)) * (z | 1UL);
	z ^= z + (z ^ (z >> 7)) * (z | 61UL);
	return z ^ (z >> 14);
}

static double seeded_random(void)
{
	uint32_t v = mullberry32(&seed_main);
	return (double)v / 4294967296.0;
}


static int32_t unsigned_shift(uint32_t *v, int s)
{
	return (int32_t)((*v) >> s);
}

static uint32_t cyrb128(const char *str)
{
	int32_t h1 = 1779033703, h2 = 3144134277, h3 = 1013904242, h4 = 2773480762;
	int32_t k;
	for (int i = 0; i < strlen(str); i++){
		k = str[i];

		h1 = h2 ^ ((h1 ^ k) * 597399067);
		h2 = h3 ^ ((h2 ^ k) * 2869860233);
		h3 = h4 ^ ((h3 ^ k) * 951274213);
		h4 = h1 ^ ((h4 ^ k) * 2716044179);
	}
	h1 = (h3 ^ unsigned_shift((uint32_t *)&h1, 18)) * 597399067;
	h2 = (h4 ^ unsigned_shift((uint32_t *)&h2, 22)) * 2869860233;
	h3 = (h1 ^ unsigned_shift((uint32_t *)&h3, 17)) * 951274213;
	h4 = (h2 ^ unsigned_shift((uint32_t *)&h4, 19)) * 2716044179;

	return h1^h2^h3^h4;
}


static void add_allowed_arrow(int *allowed_arrows, int *allowed_count, int add)
{
	if(allowed_arrows[g_items[add].sort] == add){
		return;
	}

	allowed_arrows[g_items[add].sort] = add;
	(*allowed_count)++;
}

static void fix_allowed_arrows(int *allowed_arrows)
{
	/* Shuffle everything to the start */
	for(int i=0; i<3; i++){
		int a = i+1;
		while(allowed_arrows[i] == 0 && a < 4){
			allowed_arrows[i] = allowed_arrows[a];
			allowed_arrows[a] = 0;
			a++;
		}
	}
}

static void nextItem(void)
{
	int empty_count = 0;
	int allowed_arrows[4] = {0,0,0,0};
	int allowed_count = 0;

	for(int i=0; i<16; i++){
		if(board[i].e == EMPTY){
			empty_count++;
		}else{
			if(board[i].l && board[i].l->e == EMPTY){
				add_allowed_arrow(allowed_arrows, &allowed_count, LEFT);
			}
			if(board[i].d && board[i].d->e == EMPTY){
				add_allowed_arrow(allowed_arrows, &allowed_count, DOWN);
			}
			if(board[i].u && board[i].u->e == EMPTY){
				add_allowed_arrow(allowed_arrows, &allowed_count, UP);
			}
			if(board[i].r && board[i].r->e == EMPTY){
				add_allowed_arrow(allowed_arrows, &allowed_count, RIGHT);
			}
		}
	}
	fix_allowed_arrows(allowed_arrows);

	if(empty_count == 0){
		end_game();
	}
	move_count++;

	int ncurrent = -1;
	while (true) {
		double r = seeded_random();
		double t = 0.0;
		for(int i=0; i<sizeof(g_items)/sizeof(struct farm_item); i++){
			t += g_items[i].l;
			if (t >= r) {
				ncurrent = i;
				break;
			}
		}
		if(ncurrent == -1) continue;

		if(ncurrent == LEFT || ncurrent == RIGHT || ncurrent == UP || ncurrent == DOWN){
			if(CURRENT == EMPTY){
				// no arrows first go
				continue;
			}

			bool arrow_ok = false;
			for(int i=0; i<allowed_count; i++){
				if(ncurrent == allowed_arrows[i]){
					arrow_ok = true;
					break;
				}
			}
			if(arrow_ok == false){
				if(allowed_count > 0){
					// no impossible-to-use arrows
					// if there are any valid arrows, then randomly pick one of them
					int rnd = floor(allowed_count*0.02388156927190721);
					ncurrent = allowed_arrows[rnd];
				}else{
					// if there aren't any, bail
					continue;
				}
			}
		}
		break;
	}
	CURRENT = ncurrent;
}

static bool do_move(struct board_space *b)
{
	struct board_space *match_space = NULL;

	if(CURRENT == LEFT && b->l && b->l->e == EMPTY){
		match_space = b->l;
	}else if(CURRENT == RIGHT && b->r && b->r->e == EMPTY){
		match_space = b->r;
	}else if(CURRENT == UP && b->u && b->u->e == EMPTY){
		match_space = b->u;
	}else if(CURRENT == DOWN && b->d && b->d->e == EMPTY){
		match_space = b->d;
	}

	if(match_space){
		match_space->e = b->e;
		match_space->click = CLICKS++;
		b->e = EMPTY;
		return true;
	}
	return false;
}

static void handle_click(char keypress)
{
	int pos;

	switch(keypress){
		case '1': pos = 0; break;
		case '2': pos = 1; break;
		case '3': pos = 2; break;
		case '4': pos = 3; break;
		case 'q': pos = 4; break;
		case 'w': pos = 5; break;
		case 'e': pos = 6; break;
		case 'r': pos = 7; break;
		case 'a': pos = 8; break;
		case 's': pos = 9; break;
		case 'd': pos = 10; break;
		case 'f': pos = 11; break;
		case 'z': pos = 12; break;
		case 'x': pos = 13; break;
		case 'c': pos = 14; break;
		case 'v': pos = 15; break;
		default: return;
	}

	if(CURRENT == LEFT || CURRENT == RIGHT || CURRENT == UP || CURRENT == DOWN){
		if(board[pos].e == EMPTY) return;
		bool success = do_move(&board[pos]);
		if (success) {
			coalesce();
			tick();
			nextItem();
			coalesce();
		}
	}else{
		if(board[pos].e != EMPTY) return;
		board[pos].click = CLICKS++;
		board[pos].e = CURRENT;
		coalesce();
		tick();
		nextItem();
		coalesce();
	}
}

void help(void)
{
	printf("Combine seeds to make crops\n");
	printf("     ⬛🌱🌱  →  ⬛⬛🌿\n");
	printf("     ⬛🌱⬛     ⬛⬛⬛\n\n");

	printf("Combine crops to make fields\n");
	printf("     🌿🌿🌿  →  ⬛⬛🌾\n");
	printf("     ⬛⬛⬛     ⬛⬛⬛\n\n");

	printf("Harvest nearby crops and fields for points\n");
	printf("     ⬛🌿⬛ and 🚜🌿⬛\n");
	printf("     🌾🔪🌾     🌿⬛⬛\n\n");

	printf("Water and fertilise seeds and crops to make them grow\n");
	printf("     ⬛🌱🌱  →  ⬛🌱🌿\n");
	printf("     🚰🌱💩     ⬛🌾🚰\n\n");

	printf("It's the same sequence of things for the whole day.\n");
	printf("Get the best score before the next day comes!\n\n");

	printf("Original web game is at https://kryogenix.org/farmbound\n");
	printf("This port is at https://github.com/ralight/farmbound-cli\n");
	exit(0);
}

void set_seed(void)
{
	struct tm *ti;
	time_t now = time(NULL);
	char seed_string[21];

	ti = localtime(&now);
	strftime(seed_string, sizeof(seed_string), "%d/%m/%Y", ti);

	seed_main = cyrb128(seed_string);
}


int main(int argc, char *argv[])
{
	if(argc > 1) help();

	set_seed();

	board[0].l =   NULL;		board[0].r =   &board[1];   board[0].u =   NULL;	   board[0].d =   &board[4];   board[0].pos =  0;
	board[1].l =   &board[0];   board[1].r =   &board[2];   board[1].u =   NULL;	   board[1].d =   &board[5];   board[1].pos =  1;
	board[2].l =   &board[1];   board[2].r =   &board[3];   board[2].u =   NULL;	   board[2].d =   &board[6];   board[2].pos =  2;
	board[3].l =   &board[2];   board[3].r =   NULL;		board[3].u =   NULL;	   board[3].d =   &board[7];   board[3].pos =  3;

	board[4].l =   NULL;		board[4].r =   &board[5];   board[4].u =   &board[0];  board[4].d =   &board[8];   board[4].pos =  4;
	board[5].l =   &board[4];   board[5].r =   &board[6];   board[5].u =   &board[1];  board[5].d =   &board[9];   board[5].pos =  5;
	board[6].l =   &board[5];   board[6].r =   &board[7];   board[6].u =   &board[2];  board[6].d =   &board[10];  board[6].pos =  6;
	board[7].l =   &board[6];   board[7].r =   NULL;		board[7].u =   &board[3];  board[7].d =   &board[11];  board[7].pos =  7;

	board[8].l =   NULL;		board[8].r =   &board[9];   board[8].u =   &board[4];  board[8].d =   &board[12];  board[8].pos =  8;
	board[9].l =   &board[8];   board[9].r =   &board[10];  board[9].u =   &board[5];  board[9].d =   &board[13];  board[9].pos =  9;
	board[10].l =  &board[9];   board[10].r =  &board[11];  board[10].u =  &board[6];  board[10].d =  &board[14];  board[10].pos = 10;
	board[11].l =  &board[10];  board[11].r =  NULL;		board[11].u =  &board[7];  board[11].d =  &board[15];  board[11].pos = 11;

	board[12].l =  NULL;		board[12].r =  &board[13];  board[12].u =  &board[8];  board[12].d =  NULL;        board[12].pos = 12;
	board[13].l =  &board[12];  board[13].r =  &board[14];  board[13].u =  &board[9];  board[13].d =  NULL;        board[13].pos = 13;
	board[14].l =  &board[13];  board[14].r =  &board[15];  board[14].u =  &board[10]; board[14].d =  NULL;        board[14].pos = 14;
	board[15].l =  &board[14];  board[15].r =  NULL;		board[15].u =  &board[11]; board[15].d =  NULL;        board[15].pos = 15;

	for(int i=0; i<16; i++){
		board[i].e = EMPTY;
	}

	term_fix();
	atexit(term_restore);

	nextItem();
	do{
		print_board();
		fflush(stdout);
		clear_groups();
		char c = fgetc(stdin);
		handle_click(c);
	}while(1);

	return 0;
}
