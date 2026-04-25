#include <farmbound.h>

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <utlist.h>


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

static void clear_groups(struct game_data *gd)
{
	memset(gd->groups, 0, sizeof(gd->groups));
	memset(gd->boost_used, 0, sizeof(gd->boost_used));
	gd->boosts = NULL;

	for(int i=0; i<16; i++){
		gd->board[i].next = NULL;
		gd->board[i].prev = NULL;
		gd->board[i].g = 0;
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

static void print_board(struct game_data *gd)
{
	bool have_background = false;
	bool have_score = false;

	printf("\e[2J");
	printf("\e[1,1H");
	for(int i=0; i<16; i++){
		if(gd->score_colour[i]){
			printf("\e[48;5;226m");
			have_score = true;
			gd->score_colour[i] = 0;
		}
		if(gd->background_colour[i] == COL_WATER){
			printf("\e[48;2;150;150;255m");
			have_background = true;
		}else if(gd->background_colour[i] == COL_MANURE){
			printf("\e[48;2;191;105;82m");
			have_background = true;
		}else if(gd->background_colour[i] == COL_FERTILISER){
			printf("\e[48;2;80;165;230m");
			have_background = true;
		}
		if(gd->board[i].e == EMPTY){
			printf("⬛️");
		}else{
			if(gd->background_colour[i]){
				if(gd->board[i].e_old == EMPTY){
					printf("⬛️");
				}else{
					printf("%s", g_items[gd->board[i].e_old].icon);
				}
			}else{
				printf("%s", g_items[gd->board[i].e].icon);
			}
		}
		printf("\e[0m");

		if(i == 7){
			printf("  Next:  %s\n", g_items[gd->CURRENT].icon);
		}else if(i == 11){
			printf("  Moves: %d\n", gd->move_count-1);
		}else if(i == 15){
			printf("  Score: %d\n", gd->total_score);
		}else if(i == 3){
			printf("\n");
		}
	}
	if(have_score){
		struct timespec req = {0, 200000000};
		nanosleep(&req, NULL);

		print_board(gd);
		return;
	}
	if(have_background){
		memset(gd->background_colour, 0, sizeof(gd->background_colour));

		struct timespec req = {0, 300000000};
		nanosleep(&req, NULL);

		print_board(gd);
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

static void coalesce(struct game_data *gd)
{
	// collect into groups
	clear_groups(gd);

	int groupcount = 1;
	for(int i=0; i<16; i++){
		struct board_space *b = &gd->board[i];

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
				struct board_space *nb = &gd->board[j];
				if (nb->g == leftgroup) nb->g = b->g;
			}
		} else if (b->u && b->u->e == b->e) { b->g = b->u->g; }
		else if (b->l && b->l->e == b->e) { b->g = b->l->g; }
		else { b->g = groupcount++; }
	}

	for(int i=0; i<16; i++){
		struct board_space *b = &gd->board[i];
		b->next = NULL;
		b->prev = NULL;

		if(b->g){
			DL_APPEND(gd->groups[b->g], b);
		}
	}
	bool changed = false;

	for(int i=1; i<16; i++){
		struct board_space *g = gd->groups[i], *gtmp;
		int count;

		DL_COUNT(gd->groups[i], gtmp, count);
		if(count < 3) continue;

		DL_SORT(gd->groups[i], click_cmp);

		int evolve_icon = g_items[g->e].evolve;
		if (evolve_icon != -1) { // FIELD can't be evolved, for example
			evolve(&gd->groups[i], evolve_icon);
			changed = true;
		}
	}
	if (changed){
		coalesce(gd);
	}
}

static int score_add(struct board_space *b)
{
	if(b && b->e == SCYTHE) return 2;
	else if(b && b->e == HARVESTER) return 5;
	else return 0;
}


static void boost_add_event(struct game_data *gd, struct board_space *b, struct board_space *booster, int boost_to, int reduce_booster_to)
{
	struct boost_event *ev = calloc(1, sizeof(struct boost_event));
	if(!ev) exit(1);

	ev->b = b;
	ev->booster = booster;
	ev->boost_to = g_items[b->e].evolve;
	ev->reduce_booster_to = reduce_booster_to;
	DL_APPEND(gd->boosts, ev);
}

static void boost_add(struct game_data *gd, struct board_space *b, struct board_space *other){
	if (!other || other->e == EMPTY) return;

	if (other->e == WATER && !gd->boost_used[other->pos]) {
		// there's unused water next to this field, so use it
		gd->boost_used[other->pos] = 1;
		gd->background_colour[other->pos] = COL_WATER;
		gd->background_colour[b->pos] = COL_WATER;
		boost_add_event(gd, b, other, g_items[b->e].evolve, EMPTY);
	} else if (other->e == MANURE && gd->boost_used[other->pos] < 4) {
		gd->boost_used[other->pos] += 1;
		gd->background_colour[other->pos] = COL_MANURE;
		gd->background_colour[b->pos] = COL_MANURE;
		boost_add_event(gd, b, other, g_items[b->e].evolve, WATER);
		b->click = gd->CLICKS++;
	} else if (other->e == FERTILISER && gd->boost_used[other->pos] < 10) {
		gd->boost_used[other->pos] += 1;
		gd->background_colour[other->pos] = COL_FERTILISER;
		gd->background_colour[b->pos] = COL_FERTILISER;
		boost_add_event(gd, b, other, g_items[b->e].evolve, MANURE);
		b->click = gd->CLICKS++;
	}
}

static void tick(struct game_data *gd)
{
	int score = 0;
	memset(gd->boost_used, 0, sizeof(gd->boost_used));

	gd->boosts = NULL;
	for(int i=0; i<16; i++){
		gd->board[i].next = NULL;
		gd->board[i].prev = NULL;
	}

	for(int i=0; i<16; i++){
		struct board_space *b = &gd->board[i];

		// point scoring via harvesting
		if (b->e == CROP || b->e == FIELD){
			gd->score_colour[b->pos] += score_add(b->u);
			gd->score_colour[b->pos] += score_add(b->d);
			gd->score_colour[b->pos] += score_add(b->l);
			gd->score_colour[b->pos] += score_add(b->r);
			score += gd->score_colour[b->pos];
		}

		// boosts (fertiliser, manure, water)
		if(b->e == SEED || b->e == CROP){
			boost_add(gd, b, b->l);
			boost_add(gd, b, b->u);
			boost_add(gd, b, b->d);
			boost_add(gd, b, b->r);
		}
	}
	gd->total_score += score;

	DL_SORT(gd->boosts, boost_cmp);

	struct boost_event *ev, *ev_tmp;
	DL_FOREACH_SAFE(gd->boosts, ev, ev_tmp){
		ev->booster->e = ev->reduce_booster_to;
		ev->b->e = ev->boost_to;
		DL_DELETE(gd->boosts, ev);
		free(ev);
	}
}

static void end_game(struct game_data *gd)
{
	print_board(gd);
	exit(0);
}


uint32_t mullberry32(int32_t *x) {
	uint32_t z = ((*x) += 0x6D2B79F5UL);
	z = (z ^ (z >> 15)) * (z | 1UL);
	z ^= z + (z ^ (z >> 7)) * (z | 61UL);
	return z ^ (z >> 14);
}

static double seeded_random(struct game_data *gd)
{
	uint32_t v = mullberry32(&gd->seed_main);
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

static void nextItem(struct game_data *gd)
{
	int empty_count = 0;
	int allowed_arrows[4] = {0,0,0,0};
	int allowed_count = 0;

	for(int i=0; i<16; i++){
		if(gd->board[i].e == EMPTY){
			empty_count++;
		}else{
			if(gd->board[i].l && gd->board[i].l->e == EMPTY){
				add_allowed_arrow(allowed_arrows, &allowed_count, LEFT);
			}
			if(gd->board[i].d && gd->board[i].d->e == EMPTY){
				add_allowed_arrow(allowed_arrows, &allowed_count, DOWN);
			}
			if(gd->board[i].u && gd->board[i].u->e == EMPTY){
				add_allowed_arrow(allowed_arrows, &allowed_count, UP);
			}
			if(gd->board[i].r && gd->board[i].r->e == EMPTY){
				add_allowed_arrow(allowed_arrows, &allowed_count, RIGHT);
			}
		}
	}
	fix_allowed_arrows(allowed_arrows);

	if(empty_count == 0){
		end_game(gd);
	}
	gd->move_count++;

	int ncurrent = -1;
	while (true) {
		double r = seeded_random(gd);
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
			if(gd->CURRENT == EMPTY){
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
	gd->CURRENT = ncurrent;
}

static bool do_move(struct game_data *gd, struct board_space *b)
{
	struct board_space *match_space = NULL;

	if(gd->CURRENT == LEFT && b->l && b->l->e == EMPTY){
		match_space = b->l;
	}else if(gd->CURRENT == RIGHT && b->r && b->r->e == EMPTY){
		match_space = b->r;
	}else if(gd->CURRENT == UP && b->u && b->u->e == EMPTY){
		match_space = b->u;
	}else if(gd->CURRENT == DOWN && b->d && b->d->e == EMPTY){
		match_space = b->d;
	}

	if(match_space){
		match_space->e = b->e;
		match_space->click = gd->CLICKS++;
		b->e = EMPTY;
		return true;
	}
	return false;
}

static void handle_click(struct game_data *gd, char keypress)
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

	if(gd->CURRENT == LEFT || gd->CURRENT == RIGHT || gd->CURRENT == UP || gd->CURRENT == DOWN){
		if(gd->board[pos].e == EMPTY) return;
		bool success = do_move(gd, &gd->board[pos]);
		if (success) {
			coalesce(gd);
			tick(gd);
			nextItem(gd);
			coalesce(gd);
		}
	}else{
		if(gd->board[pos].e != EMPTY) return;
		gd->board[pos].click = gd->CLICKS++;
		gd->board[pos].e = gd->CURRENT;
		coalesce(gd);
		tick(gd);
		nextItem(gd);
		coalesce(gd);
	}
}

static void help(void)
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

static void set_seed(struct game_data *gd)
{
	struct tm *ti;
	time_t now = time(NULL);
	char seed_string[21];

	ti = localtime(&now);
	strftime(seed_string, sizeof(seed_string), "%d/%m/%Y", ti);

	gd->seed_main = cyrb128(seed_string);
}

static void update_old(struct game_data *gd)
{
	for(int i=0; i<16; i++){
		gd->board[i].e_old = gd->board[i].e;
	}
}

int main(int argc, char *argv[])
{
	if(argc > 1) help();

	struct game_data gd;
	memset(&gd, 0, sizeof(gd));
	gd.CURRENT = EMPTY;

	set_seed(&gd);

	gd.board[0].l =   NULL;           gd.board[0].r =   &gd.board[1];   gd.board[0].u =   NULL;          gd.board[0].d =   &gd.board[4];   gd.board[0].pos =  0;
	gd.board[1].l =   &gd.board[0];   gd.board[1].r =   &gd.board[2];   gd.board[1].u =   NULL;          gd.board[1].d =   &gd.board[5];   gd.board[1].pos =  1;
	gd.board[2].l =   &gd.board[1];   gd.board[2].r =   &gd.board[3];   gd.board[2].u =   NULL;          gd.board[2].d =   &gd.board[6];   gd.board[2].pos =  2;
	gd.board[3].l =   &gd.board[2];   gd.board[3].r =   NULL;           gd.board[3].u =   NULL;          gd.board[3].d =   &gd.board[7];   gd.board[3].pos =  3;

	gd.board[4].l =   NULL;           gd.board[4].r =   &gd.board[5];   gd.board[4].u =   &gd.board[0];  gd.board[4].d =   &gd.board[8];   gd.board[4].pos =  4;
	gd.board[5].l =   &gd.board[4];   gd.board[5].r =   &gd.board[6];   gd.board[5].u =   &gd.board[1];  gd.board[5].d =   &gd.board[9];   gd.board[5].pos =  5;
	gd.board[6].l =   &gd.board[5];   gd.board[6].r =   &gd.board[7];   gd.board[6].u =   &gd.board[2];  gd.board[6].d =   &gd.board[10];  gd.board[6].pos =  6;
	gd.board[7].l =   &gd.board[6];   gd.board[7].r =   NULL;           gd.board[7].u =   &gd.board[3];  gd.board[7].d =   &gd.board[11];  gd.board[7].pos =  7;

	gd.board[8].l =   NULL;           gd.board[8].r =   &gd.board[9];   gd.board[8].u =   &gd.board[4];  gd.board[8].d =   &gd.board[12];  gd.board[8].pos =  8;
	gd.board[9].l =   &gd.board[8];   gd.board[9].r =   &gd.board[10];  gd.board[9].u =   &gd.board[5];  gd.board[9].d =   &gd.board[13];  gd.board[9].pos =  9;
	gd.board[10].l =  &gd.board[9];   gd.board[10].r =  &gd.board[11];  gd.board[10].u =  &gd.board[6];  gd.board[10].d =  &gd.board[14];  gd.board[10].pos = 10;
	gd.board[11].l =  &gd.board[10];  gd.board[11].r =  NULL;           gd.board[11].u =  &gd.board[7];  gd.board[11].d =  &gd.board[15];  gd.board[11].pos = 11;

	gd.board[12].l =  NULL;           gd.board[12].r =  &gd.board[13];  gd.board[12].u =  &gd.board[8];  gd.board[12].d =  NULL;           gd.board[12].pos = 12;
	gd.board[13].l =  &gd.board[12];  gd.board[13].r =  &gd.board[14];  gd.board[13].u =  &gd.board[9];  gd.board[13].d =  NULL;           gd.board[13].pos = 13;
	gd.board[14].l =  &gd.board[13];  gd.board[14].r =  &gd.board[15];  gd.board[14].u =  &gd.board[10]; gd.board[14].d =  NULL;           gd.board[14].pos = 14;
	gd.board[15].l =  &gd.board[14];  gd.board[15].r =  NULL;           gd.board[15].u =  &gd.board[11]; gd.board[15].d =  NULL;           gd.board[15].pos = 15;

	for(int i=0; i<16; i++){
		gd.board[i].e = EMPTY;
	}

	term_fix();
	atexit(term_restore);

	nextItem(&gd);
	do{
		print_board(&gd);
		fflush(stdout);
		memset(gd.score_colour, 0, sizeof(gd.score_colour));
		clear_groups(&gd);
		char c = fgetc(stdin);
		update_old(&gd);
		handle_click(&gd, c);
	}while(1);

	return 0;
}
