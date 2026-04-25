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
	fb__init_game(&gd);
	fb__set_seed(&gd, NULL);

	term_fix();
	atexit(term_restore);

	fb__next_item(&gd);
	bool ended = false;
	do{
		fb__print_board(&gd);
		memset(gd.score_colour, 0, sizeof(gd.score_colour));
		fb__clear_groups(&gd);
		char c = fgetc(stdin);
		update_old(&gd);
		ended = fb__handle_click(&gd, c);
	}while(!ended);

	fb__print_board(&gd);
	return 0;
}
