/* CS120B Final Project
	By: Salvador Esquivias
*/

#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include "nokia5110.h"
#include "nokia5110.c"
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <avr/eeprom.h>

char *itoa (int value, char *result, int base)
{
	// check that the base if valid
	if (base < 2 || base > 36) { *result = '\0'; return result; }

	char* ptr = result, *ptr1 = result, tmp_char;
	int tmp_value;

	do {
		tmp_value = value;
		value /= base;
		*ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz" [35 + (tmp_value - value * base)];
	} while ( value );

	// Apply negative sign
	if (tmp_value < 0) *ptr++ = '-';
	*ptr-- = '\0';
	while (ptr1 < ptr) {
		tmp_char = *ptr;
		*ptr--= *ptr1;
		*ptr1++ = tmp_char;
	}
	return result;
}

//timerStart
volatile unsigned char TimerFlag = 0; // TimerISR() sets this to 1. C programmer should clear to 0.

// Internal variables for mapping AVR's ISR to our cleaner TimerISR model.
unsigned long _avr_timer_M = 1; // Start count from here, down to 0. Default 1 ms.
unsigned long _avr_timer_cntcurr = 0; // Current internal count of 1ms ticks

void TimerOn() {
	// AVR timer/counter controller register TCCR1
	TCCR1B = 0x0B;// bit3 = 0: CTC mode (clear timer on compare)
	// bit2bit1bit0=011: pre-scaler /64
	// 00001011: 0x0B
	// SO, 8 MHz clock or 8,000,000 /64 = 125,000 ticks/s
	// Thus, TCNT1 register will count at 125,000 ticks/s

	// AVR output compare register OCR1A.
	OCR1A = 125;	// Timer interrupt will be generated when TCNT1==OCR1A
	// We want a 1 ms tick. 0.001 s * 125,000 ticks/s = 125
	// So when TCNT1 register equals 125,
	// 1 ms has passed. Thus, we compare to 125.
	// AVR timer interrupt mask register
	TIMSK1 = 0x02; // bit1: OCIE1A -- enables compare match interrupt

	//Initialize avr counter
	TCNT1=0;

	_avr_timer_cntcurr = _avr_timer_M;
	// TimerISR will be called every _avr_timer_cntcurr milliseconds

	//Enable global interrupts
	SREG |= 0x80; // 0x80: 1000000
}

void TimerOff() {
	TCCR1B = 0x00; // bit3bit1bit0=000: timer off
}

void TimerISR() {
	TimerFlag = 1;
}

// In our approach, the C programmer does not touch this ISR, but rather TimerISR()
ISR(TIMER1_COMPA_vect) {
	// CPU automatically calls when TCNT1 == OCR1 (every 1 ms per TimerOn settings)
	_avr_timer_cntcurr--; // Count down to 0 rather than up to TOP
	if (_avr_timer_cntcurr == 0) { // results in a more efficient compare
		TimerISR(); // Call the ISR that the user uses
		_avr_timer_cntcurr = _avr_timer_M;
	}
}

// Set TimerISR() to tick every M ms
void TimerSet(unsigned long M) {
	_avr_timer_M = M;
	_avr_timer_cntcurr = _avr_timer_M;
}
//timerEnd

//-----------------------------------------------------------------------------------------
//analog START
void InitADC(void)
{
	ADMUX|=(1<<REFS0);
	ADCSRA|=(1<<ADEN)|(1<<ADPS0)|(1<<ADPS1)|(1<<ADPS2); //ENABLE ADC, PRESCALER 128
}
uint16_t readadc(uint8_t ch)
{
	ch&=0b00000111;         //ANDing to limit input to 7
	ADMUX = (ADMUX & 0xf8)|ch;  //Clear last 3 bits of ADMUX, OR with ch
	ADCSRA|=(1<<ADSC);        //START CONVERSION
	while((ADCSRA)&(1<<ADSC));    //WAIT UNTIL CONVERSION IS COMPLETE
	return(ADC);        //RETURN ADC VALUE
}

#define buttonUp (readadc(0) > 700)
#define buttonDown (readadc(0) < 300)
#define shot (~PIND & 0x01)
#define select (~PIND & 0x04)
#define buttonTest (~PIND & 0x08)
//analog END
//------------------------------------------------------------------------------------------


//GLOBALS
char Yposition = 24; //position of player
const char minY = 3; //max player can go up
const char maxY = 45; //max player can go down
char tmpYBullet = 0; //position y of bullet
char tmpXBullet = 6; //position x of bullet
char aMiss = 0; //determines if there was a miss (aka if it reaches the end of the screen)
//char player_timer = 0; //sm period cnt for playerTick()
const char enemyXPos[7] = {50,60,60,38,71,55,40}; //array of x position of enemies
const char enemyYPos[7] = {10,22,7,15,40,38,35}; //array of y position of enemies
char enemyValid[7] = {1,1,1,1,1,1,1}; //array determining if enemy has died/can spawn in still
char enemyAlive[7] = {0,0,0,0,0,0,0}; //array that determines if enemy is on screen or not
char enemyCnt = 3; //max enemies on the screen at one time
char totalEnemyCnt = 7;
char kill = 0; //global var that passes into enemyTick from shootingTick to destroy enemy if hit
int ipos = 0; //passes into destroyEnemy()
int r = 0; // used to store rand() num
char gameStart = 0; //Starts game once you press on Easy mode
char gameOverWon = 0; //indicates the game is over and you won (passes from enemyTick() to gameOverTick())
char gameOverLoss = 0; //indicates the game is over and you lost (passes from enemyTick() to gameOverTick())
char gameOverDone = 0; //indicates the gameOver screen is done. Go back to main menu
int GameScore = 0; //keeps track of score
int highScore = 0; //where EEPROM puts highscore
char cnt = 0; //cnt for the 1sec of gameOver on and off
char totalcnt = 0; //total cnt for displaying flashing gameover sign and score
char timerCnt = 0; //timer cnt
char gameDone = 0;
char snum[5];


//---------------------------------------------------------------------------------------------
//Menu START
void block9x9(char x, char y)
{
	nokia_lcd_set_pixel(x - 1,y - 1,1);
	nokia_lcd_set_pixel(x ,y - 1,1);
	nokia_lcd_set_pixel(x + 1,y - 1,1);
	nokia_lcd_set_pixel(x - 1,y,1);
	nokia_lcd_set_pixel(x,y,1);
	nokia_lcd_set_pixel(x + 1,y,1);
	nokia_lcd_set_pixel(x - 1,y + 1,1);
	nokia_lcd_set_pixel(x,y + 1,1);
	nokia_lcd_set_pixel(x + 1,y + 1,1);
}

void createHugeAlien()
{
	block9x9(50,23);
	block9x9(50,26);
	block9x9(50,29);
	block9x9(53,20);
	block9x9(53,23);
	block9x9(56,11);
	block9x9(56,17);
	block9x9(56,20);
	block9x9(56,23);
	block9x9(56,26);
	block9x9(56,29);
	block9x9(59,14);
	block9x9(59,17);
	block9x9(59,23);
	block9x9(59,26);
	block9x9(59,32);
	block9x9(62,17);
	block9x9(62,20);
	block9x9(62,23);
	block9x9(62,26);
	block9x9(65,17);
	block9x9(65,20);
	block9x9(65,23);
	block9x9(65,26);
	block9x9(68,14);
	block9x9(68,17);
	block9x9(68,23);
	block9x9(68,26);
	block9x9(68,32);
	block9x9(71,11);
	block9x9(71,17);
	block9x9(71,20);
	block9x9(71,23);
	block9x9(71,26);
	block9x9(71,29);
	block9x9(74,20);
	block9x9(74,23);
	block9x9(77,23);
	block9x9(77,26);
	block9x9(77,29);
}

void createHugeAlien2()
{
	block9x9(50,23);
	block9x9(50,14);
	block9x9(50,17);
	block9x9(50,20);
	block9x9(53,20);
	block9x9(53,23);
	block9x9(56,11);
	block9x9(56,17);
	block9x9(56,20);
	block9x9(56,23);
	block9x9(56,26);
	block9x9(56,29);
	block9x9(59,14);
	block9x9(59,17);
	block9x9(59,23);
	block9x9(59,26);
	block9x9(53,32);
	block9x9(62,17);
	block9x9(62,20);
	block9x9(62,23);
	block9x9(62,26);
	block9x9(65,17);
	block9x9(65,20);
	block9x9(65,23);
	block9x9(65,26);
	block9x9(68,14);
	block9x9(68,17);
	block9x9(68,23);
	block9x9(68,26);
	block9x9(74,32);
	block9x9(71,11);
	block9x9(71,17);
	block9x9(71,20);
	block9x9(71,23);
	block9x9(71,26);
	block9x9(71,29);
	block9x9(74,20);
	block9x9(74,23);
	block9x9(77,23);
	block9x9(77,17);
	block9x9(77,14);
	block9x9(77,20);
}

void createAlien(char x, char y)
{
	nokia_lcd_set_pixel(x,y,1); //orgin
	nokia_lcd_set_pixel(x ,y + 1,1); // (O - 1,O)
	nokia_lcd_set_pixel(x,y - 1,1); // (O + 1,O)
	nokia_lcd_set_pixel(x,y - 2,1); // (O + 2,O)
	
	nokia_lcd_set_pixel(x - 1,y + 1,1); // (O-1,O+1)
	nokia_lcd_set_pixel(x - 1,y,1); // (O,O+1)
	nokia_lcd_set_pixel(x - 1,y - 2,1); // (O+1,O+1)
	nokia_lcd_set_pixel(x - 1,y - 3,1); // (O+2,O+1)
	nokia_lcd_set_pixel(x - 1,y + 3,1);
	
	nokia_lcd_set_pixel(x - 2,y - 4,1); // (O-3,O+2)
	nokia_lcd_set_pixel(x - 2,y - 2,1); // (O-1,O+2)
	nokia_lcd_set_pixel(x - 2,y - 1,1); // (O,O+2)
	nokia_lcd_set_pixel(x - 2,y,1); // (O+2,O+2)
	nokia_lcd_set_pixel(x - 2,y + 1,1); // (O+3,O+2)
	nokia_lcd_set_pixel(x - 2,y + 2,1); // (O-2,O+3)
	
	nokia_lcd_set_pixel(x - 3,y - 1,1); // (O-1,O+3)
	nokia_lcd_set_pixel(x -3 ,y,1); // (O,O+3)
	
	nokia_lcd_set_pixel(x - 4,y,1); // (O+1,O+3)
	nokia_lcd_set_pixel(x - 4,y + 1,1); // (O+2,O+3)
	nokia_lcd_set_pixel(x - 4,y + 2,1); // (O+4,O+3)
	
	nokia_lcd_set_pixel(x + 1,y - 2,1); // (O,O+4)
	nokia_lcd_set_pixel(x + 1,y - 1,1); // (O+1,O+4)
	nokia_lcd_set_pixel(x + 1,y,1); // (O-2,O+5)
	nokia_lcd_set_pixel(x + 1,y + 1,1); // (O-1,O+5)
	
	nokia_lcd_set_pixel(x + 2,y - 3,1); // (O,O+5)
	nokia_lcd_set_pixel(x + 2,y - 2,1); // (O-3,O-1)
	nokia_lcd_set_pixel(x + 2,y,1); // (O-1,O-1)
	nokia_lcd_set_pixel(x + 2,y + 1,1); // (O,O-1)
	nokia_lcd_set_pixel(x + 2,y + 3,1);
	
	nokia_lcd_set_pixel(x + 3,y - 4,1); // (O+2,O-1)
	nokia_lcd_set_pixel(x + 3,y - 2,1); // (O+3,O-1)
	nokia_lcd_set_pixel(x + 3,y - 1,1); // (O-2,O-2)
	nokia_lcd_set_pixel(x + 3,y,1); // (O-1,O-2)
	nokia_lcd_set_pixel(x + 3,y + 1,1); // (O,O-2)
	nokia_lcd_set_pixel(x + 3,y + 2,1); // (O+1,O-2)
	
	nokia_lcd_set_pixel(x + 4,y - 1,1); // (O+2,O-2)
	nokia_lcd_set_pixel(x + 4,y,1); // (O+4,O-2)
	
	nokia_lcd_set_pixel(x + 5,y,1); // (O,O-3)
	nokia_lcd_set_pixel(x + 5,y + 1,1); // (O+1,O-3)
	nokia_lcd_set_pixel(x + 5,y + 2,1); // (O-2,O-4)
}

void createAlien2(char x, char y)
{
	nokia_lcd_set_pixel(x,y,1); //orgin
	nokia_lcd_set_pixel(x ,y + 1,1); // (O - 1,O)
	nokia_lcd_set_pixel(x,y - 1,1); // (O + 1,O)
	nokia_lcd_set_pixel(x,y - 2,1); // (O + 2,O)
	
	nokia_lcd_set_pixel(x - 1,y + 1,1); // (O-1,O+1)
	nokia_lcd_set_pixel(x - 1,y,1); // (O,O+1)
	nokia_lcd_set_pixel(x - 1,y - 2,1); // (O+1,O+1)
	nokia_lcd_set_pixel(x - 1,y - 3,1); // (O+2,O+1)
	nokia_lcd_set_pixel(x - 3,y + 3,1);
	
	nokia_lcd_set_pixel(x - 2,y - 4,1); // (O-3,O+2)
	nokia_lcd_set_pixel(x - 2,y - 2,1); // (O-1,O+2)
	nokia_lcd_set_pixel(x - 2,y - 1,1); // (O,O+2)
	nokia_lcd_set_pixel(x - 2,y,1); // (O+2,O+2)
	nokia_lcd_set_pixel(x - 2,y + 1,1); // (O+3,O+2)
	nokia_lcd_set_pixel(x - 2,y + 2,1); // (O-2,O+3)
	
	nokia_lcd_set_pixel(x - 3,y - 1,1); // (O-1,O+3)
	nokia_lcd_set_pixel(x - 3 ,y,1); // (O,O+3)
	
	nokia_lcd_set_pixel(x - 4,y,1); // (O+1,O+3)
	nokia_lcd_set_pixel(x - 4,y - 1,1); // (O+2,O+3)
	nokia_lcd_set_pixel(x - 4,y - 2,1); // (O+4,O+3)
	nokia_lcd_set_pixel(x - 4,y -3,1);
	
	nokia_lcd_set_pixel(x + 1,y - 2,1); // (O,O+4)
	nokia_lcd_set_pixel(x + 1,y - 1,1); // (O+1,O+4)
	nokia_lcd_set_pixel(x + 1,y,1); // (O-2,O+5)
	nokia_lcd_set_pixel(x + 1,y + 1,1); // (O-1,O+5)
	
	nokia_lcd_set_pixel(x + 2,y - 3,1); // (O,O+5)
	nokia_lcd_set_pixel(x + 2,y - 2,1); // (O-3,O-1)
	nokia_lcd_set_pixel(x + 2,y,1); // (O-1,O-1)
	nokia_lcd_set_pixel(x + 2,y + 1,1); // (O,O-1)
	nokia_lcd_set_pixel(x + 4,y + 3,1);
	
	nokia_lcd_set_pixel(x + 3,y - 4,1); // (O+2,O-1)
	nokia_lcd_set_pixel(x + 3,y - 2,1); // (O+3,O-1)
	nokia_lcd_set_pixel(x + 3,y - 1,1); // (O-2,O-2)
	nokia_lcd_set_pixel(x + 3,y,1); // (O-1,O-2)
	nokia_lcd_set_pixel(x + 3,y + 1,1); // (O,O-2)
	nokia_lcd_set_pixel(x + 3,y + 2,1); // (O+1,O-2)
	
	nokia_lcd_set_pixel(x + 4,y - 1,1); // (O+2,O-2)
	nokia_lcd_set_pixel(x + 4,y,1); // (O+4,O-2)
	
	nokia_lcd_set_pixel(x + 5,y,1); // (O,O-3)
	nokia_lcd_set_pixel(x + 5,y - 1,1); // (O+1,O-3)
	nokia_lcd_set_pixel(x + 5,y - 2,1); // (O-2,O-4)
	nokia_lcd_set_pixel(x + 5, y - 3,1);
}


enum menuStates {MenuInit, Title, Option1, Option2, waitForGame, Credits, Reset} menu;
void menuTick()
{
	switch(menu) //Transitions
	{
		case MenuInit:
			menu = Title;
			break;
		
		case Title:
			if(select)
			{
				menu = Option1;
				
			}
			else
			{
				menu = Title;
			}
			break;
		
		case Option1:
			if(select)
			{
				gameStart = 1;
				menu = waitForGame;
				nokia_lcd_clear();
			}
			else if(buttonDown)
			{
				menu = Option2;
			}
			else
			{
				menu = Option1;
			}
			break;
		
		case Option2:
			if(select)
			{
				menu = Credits;
			}
			else if(buttonUp)
			{
				menu = Option1;
			}
			else
			{
				menu = Option2;
			}
			break;
			
		case waitForGame:
			if(gameOverDone)
			{
				menu = Reset;
			}
			else
			{
				menu = waitForGame;
			}
			break;
			
		case Credits:
			if(shot)
			{
				menu = Option2;
			}
			else
			{
				menu = Credits;
			}
			break;
		
		case Reset:
			menu = Title;
			break;
		
		
		default:
			menu = MenuInit;
	}
	
	switch(menu) //actions
	{
		case MenuInit:
			break;
		
		case Title:
			nokia_lcd_clear();
			nokia_lcd_set_pixel(0, 0, 1);
			nokia_lcd_set_pixel(0,1,1);
			nokia_lcd_set_pixel(0,2,1);
			nokia_lcd_set_pixel(1,0,1);
			nokia_lcd_set_pixel(1,1,1);
			nokia_lcd_set_pixel(1,2,1);
			nokia_lcd_set_pixel(82,0,1);
			nokia_lcd_set_pixel(82,1,1);
			nokia_lcd_set_pixel(82,2,1);
			nokia_lcd_set_pixel(83,0,1);
			nokia_lcd_set_pixel(83,1,1);
			nokia_lcd_set_pixel(83,2,1);
			nokia_lcd_set_cursor(15,10);
			nokia_lcd_write_string("Alien", 2);
			nokia_lcd_set_cursor(15,30);
			nokia_lcd_write_string("World", 2);
			createAlien(5,5);
			createAlien2(17,5);
			createAlien(29,5);
			createAlien2(41,5);
			createAlien(53,5);
			createAlien2(65,5);
			createAlien(77,5);
			nokia_lcd_render();
			break;
		
		case Option1:
			nokia_lcd_clear();
			createHugeAlien();
			nokia_lcd_set_cursor(0,0);
			nokia_lcd_write_string("Select Mode", 1);
			nokia_lcd_set_cursor(0, 20);
			nokia_lcd_write_string("> Easy", 1);
			nokia_lcd_set_cursor(0, 30);
			nokia_lcd_write_string("  Credit", 1);
			nokia_lcd_render();
			break;
		
		case Option2:
			nokia_lcd_clear();
			createHugeAlien2();
			nokia_lcd_set_cursor(0,0);
			nokia_lcd_write_string("Select Mode", 1);
			nokia_lcd_set_cursor(0, 20);
			nokia_lcd_write_string("  Easy", 1);
			nokia_lcd_set_cursor(0, 30);
			nokia_lcd_write_string("> Credit", 1);
			nokia_lcd_render();
			break;
		
		case waitForGame:
			break;
		
		case Credits:
			nokia_lcd_clear();
			nokia_lcd_set_cursor(2,2);
			nokia_lcd_write_string("Alien", 1);
			nokia_lcd_set_cursor(2,22);
			nokia_lcd_write_string("World",2);
			nokia_lcd_set_cursor(2,37);
			nokia_lcd_write_string("By: Salvador E", 1);
			nokia_lcd_render();
			break;
		
		case Reset:
			Yposition = 24; //position of player
			tmpYBullet = 0; //position y of bullet
			tmpXBullet = 6; //position x of bullet
			aMiss = 0; //determines if there was a miss (aka if it reaches the end of the screen)
			kill = 0; //global var that passes into enemyTick from shootingTick to destroy enemy if hit
			ipos = 0; //passes into destroyEnemy()
			r = 0; // used to store rand() num
			gameStart = 0; //Starts game once you press on Easy mode
			gameOverWon = 0; //indicates the game is over (passes from enemyTick() to gameOverTick())
			gameOverLoss = 0;
			gameOverDone = 0; //indicates the gameOver screen is done. Go back to main menu
			GameScore = 0; //keeps track of score
			cnt = 0; //cnt for the 1sec of gameOver on and off
			totalcnt = 0; //total cnt for displaying flashing gameover sign and score
			enemyCnt = 3; //max enemies on the screen at one time
			timerCnt = 0; //timer cnt
			gameDone = 0;
			for(int i = 0; i < 7; ++i)
			{
				enemyValid[i] = 1;
				enemyAlive[i] = 0;
			}
	}
}
//Menu END
//---------------------------------------------------------------------------------------------


//---------------------------------------------------------------------------------------------
//PLAYER MOVEMENT
void createPlayer()
{
	nokia_lcd_set_pixel(2,Yposition - 1,1);
	nokia_lcd_set_pixel(2,Yposition + 1,1);
	nokia_lcd_set_pixel(3,Yposition - 2,1);
	nokia_lcd_set_pixel(3,Yposition - 1,1);
	nokia_lcd_set_pixel(3,Yposition,1);
	nokia_lcd_set_pixel(3,Yposition + 1,1);
	nokia_lcd_set_pixel(3,Yposition + 2,1);
	nokia_lcd_set_pixel(4,Yposition - 1,1);
	nokia_lcd_set_pixel(4,Yposition,1);
	nokia_lcd_set_pixel(4,Yposition + 1,1);
	nokia_lcd_set_pixel(5,Yposition,1);
	nokia_lcd_render();
}

void movePlayerUp()
{
	//clear
	nokia_lcd_set_pixel(2,Yposition - 1,0);
	nokia_lcd_set_pixel(2,Yposition + 1,0);
	nokia_lcd_set_pixel(3,Yposition - 2,0);
	nokia_lcd_set_pixel(3,Yposition - 1,0);
	nokia_lcd_set_pixel(3,Yposition,0);
	nokia_lcd_set_pixel(3,Yposition + 1,0);
	nokia_lcd_set_pixel(3,Yposition + 2,0);
	nokia_lcd_set_pixel(4,Yposition - 1,0);
	nokia_lcd_set_pixel(4,Yposition,0);
	nokia_lcd_set_pixel(4,Yposition + 1,0);
	nokia_lcd_set_pixel(5,Yposition,0);
	
	//move up
	nokia_lcd_set_pixel(2,(Yposition - 1) - 1,1);
	nokia_lcd_set_pixel(2,(Yposition + 1) - 1,1);
	nokia_lcd_set_pixel(3,(Yposition - 2) - 1,1);
	nokia_lcd_set_pixel(3,(Yposition - 1) - 1,1);
	nokia_lcd_set_pixel(3,(Yposition) - 1,1);
	nokia_lcd_set_pixel(3,(Yposition + 1) - 1,1);
	nokia_lcd_set_pixel(3,(Yposition + 2) - 1,1);
	nokia_lcd_set_pixel(4,(Yposition - 1) - 1,1);
	nokia_lcd_set_pixel(4,(Yposition) - 1,1);
	nokia_lcd_set_pixel(4,(Yposition + 1) - 1,1);
	nokia_lcd_set_pixel(5,(Yposition) - 1,1);
	nokia_lcd_render();
}

void movePlayerDown()
{
	//clear
	nokia_lcd_set_pixel(2,Yposition - 1,0);
	nokia_lcd_set_pixel(2,Yposition + 1,0);
	nokia_lcd_set_pixel(3,Yposition - 2,0);
	nokia_lcd_set_pixel(3,Yposition - 1,0);
	nokia_lcd_set_pixel(3,Yposition,0);
	nokia_lcd_set_pixel(3,Yposition + 1,0);
	nokia_lcd_set_pixel(3,Yposition + 2,0);
	nokia_lcd_set_pixel(4,Yposition - 1,0);
	nokia_lcd_set_pixel(4,Yposition,0);
	nokia_lcd_set_pixel(4,Yposition + 1,0);
	nokia_lcd_set_pixel(5,Yposition,0);
	nokia_lcd_render();
	
	//moveDown
	nokia_lcd_set_pixel(2,(Yposition - 1) + 1,1);
	nokia_lcd_set_pixel(2,(Yposition + 1) + 1,1);
	nokia_lcd_set_pixel(3,(Yposition - 2) + 1,1);
	nokia_lcd_set_pixel(3,(Yposition - 1) + 1,1);
	nokia_lcd_set_pixel(3,(Yposition) + 1,1);
	nokia_lcd_set_pixel(3,(Yposition + 1) + 1,1);
	nokia_lcd_set_pixel(3,(Yposition + 2) + 1,1);
	nokia_lcd_set_pixel(4,(Yposition - 1) + 1,1);
	nokia_lcd_set_pixel(4,(Yposition) + 1,1);
	nokia_lcd_set_pixel(4,(Yposition + 1) + 1,1);
	nokia_lcd_set_pixel(5,(Yposition) + 1,1);
	nokia_lcd_render();
}


enum PlayerMovement{Init, PlayerWait, CreateP, Static, moveUp, moveDown} playerstate;
void playerTick()
{
	switch(playerstate) //transition
	{
		case Init:
			playerstate = PlayerWait;
			break;
			
		case PlayerWait:
			if(gameStart)
			{
				playerstate = CreateP;
			}
			else
			{
				playerstate = PlayerWait;
			}
			break;
			
		case CreateP:
			playerstate = Static;
			break;
			
		case Static:
			if(buttonDown && (Yposition <= maxY)) //movedown
			{
				playerstate = moveDown;
			}
			else if(buttonUp && (Yposition >= minY)) //moveUp
			{
				playerstate = moveUp;
			}
			/*else if(shot)
			{
				shooting = 1;
				fired = 1;
				playerstate = Static;
			}*/
			else
			{
				playerstate = Static;
			} 
			break;
			
		case moveUp:
			if(buttonDown && (Yposition <= maxY)) //moveDown
			{
				playerstate = moveDown;	
			}
			else if(buttonUp && (Yposition >= minY)) //stilll moving up
			{
				playerstate = moveUp;
			}
			/*else if(shot)
			{
				shooting = 1;
				fired = 1;
				playerstate = Static;
			}*/
			else
			{
				playerstate = Static;
			}
			break;
			
		case moveDown:
			if(buttonUp && (Yposition >= minY)) //moveUP
			{
				playerstate = moveUp;
			}
			else if(buttonDown && (Yposition <= maxY)) //stilll moving down
			{
				playerstate = moveDown;
			}
			/*else if(shot)
			{
				shooting = 1;
				fired = 1;
				playerstate = Static;
			}*/
			else
			{
				playerstate = Static;
			}
			break;
		
		default:
			playerstate = Init;
	}
	
	switch(playerstate) //actions
	{
		case Init:
			break;
			
		case PlayerWait:
			break;
			
		case CreateP:
			createPlayer();
			break;
		
		case Static:
			break;
		
		case moveUp:
			movePlayerUp();
			Yposition--;
			break;
		
		case moveDown:
			movePlayerDown();
			Yposition++;
			break;
	}
}
//PLAYER MOVEMENT END
//---------------------------------------------------------------------------------------------


//---------------------------------------------------------------------------------------------
//SHOOTING START
int aHit(char Xval, char Yval)
{
	for(int i = 0; i < 7; ++i)
	{
		char xPosMin = enemyXPos[i] - 3; //min value of hit box of the alien in the x coordinate
		char yPosMin = enemyYPos[i] - 4; //min value of hit box of alien in the y coordinate
		char yPosMax = enemyYPos[i] + 5; //max value of hit box of alien in the y coordinate
		if( (tmpXBullet >= xPosMin) && ( (tmpYBullet >= yPosMin) && (tmpYBullet <= yPosMax) ) && (enemyAlive[i] == 1) && (enemyValid[i] == 1) )
		{
			ipos = i;
			nokia_lcd_set_pixel(tmpXBullet, tmpYBullet, 0);
			nokia_lcd_set_pixel((tmpXBullet - 1),tmpYBullet,0);
			nokia_lcd_render();
			
			return 1;
		}
	}
	return 0;
}

void initBullet()
{
	tmpYBullet = Yposition;
	nokia_lcd_set_pixel(tmpXBullet,tmpYBullet,1);
	nokia_lcd_set_pixel(tmpXBullet - 1,tmpYBullet,1);
	nokia_lcd_render();
}

void moveBullet()
{
	if(tmpXBullet >= 83) //miss
	{
		aMiss = 1;
		nokia_lcd_set_pixel(tmpXBullet,tmpYBullet,0);
		nokia_lcd_set_pixel((tmpXBullet - 1),tmpYBullet,0);
		nokia_lcd_render();
		
	}
	else if(tmpXBullet < 83)//continue
	{
		//clear old bullet
		nokia_lcd_set_pixel(tmpXBullet,tmpYBullet, 0);
		nokia_lcd_set_pixel((tmpXBullet - 1),tmpYBullet,0);
		nokia_lcd_render();
		//new position
		nokia_lcd_set_pixel(tmpXBullet + 1,tmpYBullet,1);
		nokia_lcd_set_pixel((tmpXBullet - 1) + 1,tmpYBullet,1);
		nokia_lcd_render();
		
	}
}
enum Shooting{ShootInit, WaitForStart, shootWait, ShotBullet, BulletTraveling, BulletHit} bulletstate;

void shootingTick()
{
	switch(bulletstate)
	{
		case ShootInit:
			bulletstate = WaitForStart;
			break;
			
		case WaitForStart:
			if (gameStart)
			{
				bulletstate = shootWait;
			}
			else
			{
				bulletstate = WaitForStart;
			}
			break;
		
		case shootWait:
			if(shot)
			{
				bulletstate = ShotBullet;
			}
			else
			{
				bulletstate = shootWait;
			}
			break;
		
		case ShotBullet:
			bulletstate = BulletTraveling;
			break;
			
		case BulletTraveling:
			if(aHit(tmpXBullet,tmpYBullet))
			{
				bulletstate = BulletHit;
				tmpXBullet = 6;
			}
			else if(aMiss)
			{
				bulletstate = shootWait;
				tmpXBullet = 6;
				aMiss = 0;
			}
			else
			{
				bulletstate = BulletTraveling;
			}
			break;
		
		case BulletHit:
			bulletstate = shootWait;
			break;
			
		default:
			bulletstate = ShootInit;
			
	}
	
	switch (bulletstate) // Actions
	{
		case ShootInit:
			break;
			
		case WaitForStart:
			break;
		
		case shootWait:
			break;
			
		case ShotBullet:
			initBullet();
			break;
		
		case BulletTraveling:
			moveBullet();
			tmpXBullet++;
			break;
		
		case BulletHit:
			kill = 1;
			GameScore += 5;
			break;		
	}
}
//SHOOTING END
//---------------------------------------------------------------------------------------------


//----------------------------------------------------------------------------------------------
//Enemy START
void createEnemy(char x, char y)
{
	nokia_lcd_set_pixel(x,y,1); //orgin
	nokia_lcd_set_pixel(x - 1 ,y,1); // (O - 1,O)
	nokia_lcd_set_pixel(x + 1,y,1); // (O + 1,O)
	nokia_lcd_set_pixel(x + 2,y,1); // (O + 2,O)
	
	nokia_lcd_set_pixel(x - 1,y + 1,1); // (O-1,O+1)
	nokia_lcd_set_pixel(x,y + 1,1); // (O,O+1)
	nokia_lcd_set_pixel(x + 1,y + 1,1); // (O+1,O+1)
	nokia_lcd_set_pixel(x + 2,y + 1,1); // (O+2,O+1)
	
	nokia_lcd_set_pixel(x - 3,y + 2,1); // (O-3,O+2)
	nokia_lcd_set_pixel(x - 1,y + 2,1); // (O-1,O+2)
	nokia_lcd_set_pixel(x,y + 2,1); // (O,O+2)
	nokia_lcd_set_pixel(x + 2,y + 2,1); // (O+2,O+2)
	nokia_lcd_set_pixel(x + 3,y + 2,1); // (O+3,O+2)
	
	nokia_lcd_set_pixel(x - 2,y + 3,1); // (O-2,O+3)
	nokia_lcd_set_pixel(x - 1,y + 3,1); // (O-1,O+3)
	nokia_lcd_set_pixel(x,y + 3,1); // (O,O+3)
	nokia_lcd_set_pixel(x + 1,y + 3,1); // (O+1,O+3)
	nokia_lcd_set_pixel(x + 2,y + 3,1); // (O+2,O+3)
	nokia_lcd_set_pixel(x + 4,y + 3,1); // (O+4,O+3)
	
	nokia_lcd_set_pixel(x,y + 4,1); // (O,O+4)
	nokia_lcd_set_pixel(x + 1,y + 4,1); // (O+1,O+4)
	
	nokia_lcd_set_pixel(x - 2,y + 5,1); // (O-2,O+5)
	nokia_lcd_set_pixel(x - 1,y + 5,1); // (O-1,O+5)
	nokia_lcd_set_pixel(x,y + 5,1); // (O,O+5)
	
	nokia_lcd_set_pixel(x - 3,y - 1,1); // (O-3,O-1)
	nokia_lcd_set_pixel(x - 1,y - 1,1); // (O-1,O-1)
	nokia_lcd_set_pixel(x,y - 1,1); // (O,O-1)
	nokia_lcd_set_pixel(x + 2,y - 1,1); // (O+2,O-1)
	nokia_lcd_set_pixel(x + 3,y - 1,1); // (O+3,O-1)
	
	nokia_lcd_set_pixel(x - 2,y - 2,1); // (O-2,O-2)
	nokia_lcd_set_pixel(x - 1,y - 2,1); // (O-1,O-2)
	nokia_lcd_set_pixel(x,y - 2,1); // (O,O-2)
	nokia_lcd_set_pixel(x + 1,y - 2,1); // (O+1,O-2)
	nokia_lcd_set_pixel(x + 2,y - 2,1); // (O+2,O-2)
	nokia_lcd_set_pixel(x + 4,y - 2,1); // (O+4,O-2)
	
	nokia_lcd_set_pixel(x,y - 3,1); // (O,O-3)
	nokia_lcd_set_pixel(x + 1,y - 3,1); // (O+1,O-3)
	
	nokia_lcd_set_pixel(x - 2,y - 4,1); // (O-2,O-4)
	nokia_lcd_set_pixel(x - 1,y - 4,1); // (O-1,O-4)
	nokia_lcd_set_pixel(x,y - 4,1); // (O,O-4)
	nokia_lcd_render();
}

void firstWave()
{	
	for(int i = 0; i < 3; ++i)
	{
		r = rand() % 7;
		while(enemyValid[r] == 0)
		{
			r = rand() % 7;
		}
		createEnemy(enemyXPos[r], enemyYPos[r]);
		enemyAlive[r] = 1;
	}
	
}

void spawnAnother()
{
	r = rand() % 7;
	while(enemyValid[r] == 0)
	{
		r = rand() % 7;
	}
	createEnemy(enemyXPos[r], enemyYPos[r]);
	enemyAlive[r] = 1;
	enemyCnt++;
}

void destroyEnemy(char x, char y)
{
	nokia_lcd_set_pixel(x,y,0); //orgin
	nokia_lcd_set_pixel(x - 1 ,y,0); // (O - 1,O)
	nokia_lcd_set_pixel(x + 1,y,0); // (O + 1,O)
	nokia_lcd_set_pixel(x + 2,y,0); // (O + 2,O)
	
	nokia_lcd_set_pixel(x - 1,y + 1,0); // (O-1,O+1)
	nokia_lcd_set_pixel(x,y + 1,0); // (O,O+1)
	nokia_lcd_set_pixel(x + 1,y + 1,0); // (O+1,O+1)
	nokia_lcd_set_pixel(x + 2,y + 1,0); // (O+2,O+1)
	
	nokia_lcd_set_pixel(x - 3,y + 2,0); // (O-3,O+2)
	nokia_lcd_set_pixel(x - 1,y + 2,0); // (O-1,O+2)
	nokia_lcd_set_pixel(x,y + 2,0); // (O,O+2)
	nokia_lcd_set_pixel(x + 2,y + 2,0); // (O+2,O+2)
	nokia_lcd_set_pixel(x + 3,y + 2,0); // (O+3,O+2)
	
	nokia_lcd_set_pixel(x - 2,y + 3,0); // (O-2,O+3)
	nokia_lcd_set_pixel(x - 1,y + 3,0); // (O-1,O+3)
	nokia_lcd_set_pixel(x,y + 3,0); // (O,O+3)
	nokia_lcd_set_pixel(x + 1,y + 3,0); // (O+1,O+3)
	nokia_lcd_set_pixel(x + 2,y + 3,0); // (O+2,O+3)
	nokia_lcd_set_pixel(x + 4,y + 3,0); // (O+4,O+3)
	
	nokia_lcd_set_pixel(x,y + 4,0); // (O,O+4)
	nokia_lcd_set_pixel(x + 1,y + 4,0); // (O+1,O+4)
	
	nokia_lcd_set_pixel(x - 2,y + 5,0); // (O-2,O+5)
	nokia_lcd_set_pixel(x - 1,y + 5,0); // (O-1,O+5)
	nokia_lcd_set_pixel(x,y + 5,0); // (O,O+5)
	
	nokia_lcd_set_pixel(x - 3,y - 1,0); // (O-3,O-1)
	nokia_lcd_set_pixel(x - 1,y - 1,0); // (O-1,O-1)
	nokia_lcd_set_pixel(x,y - 1,0); // (O,O-1)
	nokia_lcd_set_pixel(x + 2,y - 1,0); // (O+2,O-1)
	nokia_lcd_set_pixel(x + 3,y - 1,0); // (O+3,O-1)
	
	nokia_lcd_set_pixel(x - 2,y - 2,0); // (O-2,O-2)
	nokia_lcd_set_pixel(x - 1,y - 2,0); // (O-1,O-2)
	nokia_lcd_set_pixel(x,y - 2,0); // (O,O-2)
	nokia_lcd_set_pixel(x + 1,y - 2,0); // (O+1,O-2)
	nokia_lcd_set_pixel(x + 2,y - 2,0); // (O+2,O-2)
	nokia_lcd_set_pixel(x + 4,y - 2,0); // (O+4,O-2)
	
	nokia_lcd_set_pixel(x,y - 3,0); // (O,O-3)
	nokia_lcd_set_pixel(x + 1,y - 3,0); // (O+1,O-3)
	
	nokia_lcd_set_pixel(x - 2,y - 4,0); // (O-2,O-4)
	nokia_lcd_set_pixel(x - 1,y - 4,0); // (O-1,O-4)
	nokia_lcd_set_pixel(x,y - 4,0); // (O,O-4)
	nokia_lcd_render();
	if((enemyValid[ipos] == 1) && (enemyAlive[ipos] == 1))
	{
		enemyValid[ipos] = 0;
		enemyAlive[ipos] = 0;
		enemyCnt--;
	}
	
}

enum EnemyGame{EnemyInit, EnemyWaitForGame, SpawnFirstWave, EnemyWait, SpawnMore, KillEnemy} enemystate;

void enemyTick()
{
	switch(enemystate) //transition
	{
		case EnemyInit:
			enemystate = EnemyWaitForGame;
			break;
			
		case EnemyWaitForGame:
			if(gameStart)
			{
				enemystate = SpawnFirstWave;
			}
			else
			{
				enemystate = EnemyWaitForGame;
			}
			break;
		
		case SpawnFirstWave:
		enemystate = EnemyWait;
		break;
		
		case EnemyWait:
		if(enemyCnt <= 2)
		{
			enemystate = SpawnMore;
		}
		else if(kill)
		{
			enemystate = KillEnemy;
			kill = 0;
		}
		else if(buttonTest)
		{
			gameOverWon = 1;
			gameStart = 0;
			enemystate = EnemyInit;
		}
		else if(gameDone)
		{
			gameOverLoss = 1;
			gameStart = 0;
			enemystate = EnemyInit;
		}
		else
		{
			enemystate = EnemyWait;
		}
		break;
		
		case SpawnMore:
		enemystate = EnemyWait;
		break;
		
		case KillEnemy:
			enemystate = EnemyWait;
			break;
		
		default:
		enemystate = EnemyInit;
	}
	
	switch (enemystate)
	{
		case EnemyInit:
		break;
		
		case EnemyWaitForGame:
			break;
		
		case SpawnFirstWave:
		firstWave();
		break;
		
		case EnemyWait:
		break;
		
		case SpawnMore:
		spawnAnother();
		enemyCnt++;
		break;
		
		case KillEnemy:
		destroyEnemy(enemyXPos[ipos],enemyYPos[ipos]);
		break;
	}
}
//Enemy END
//----------------------------------------------------------------------------------------------


//----------------------------------------------------------------------------------------------
//GameOver START
enum GameOver{GameOverInit, GameOverWait, YouWon, YouLost, GameOverOn, GameOverOff, Score} gameoverstate;

void gameOverTick()
{
	switch(gameoverstate) //transitions
	{
		case GameOverInit:
		gameoverstate = GameOverWait;
		break;
		
		case GameOverWait:
		if(gameOverWon)
		{
			gameoverstate = YouWon;
		}
		else if (gameOverLoss)
		{
			gameoverstate = YouLost;
		}
		else
		{
			gameoverstate = GameOverWait;
		}
		break;
		
		case YouWon:
			if(cnt <= 20)
			{
				gameoverstate = YouWon;
				cnt++;
			}
			else
			{
				gameoverstate = GameOverOn;
				cnt = 0;
			}
			break;
			
		case YouLost:
			if (cnt <= 20)
			{
				gameoverstate = YouLost;
				cnt++;	
			}
			else
			{
				gameoverstate = GameOverOn;
				cnt = 0;
			}
			break;
			
		
		case GameOverOn:
		if(cnt <= 10)
		{
			gameoverstate = GameOverOn;
			cnt++;
			totalcnt++;
		}
		else if(totalcnt >= 50)
		{
			gameoverstate = Score;
			cnt = 0;
			totalcnt = 0;
		}
		else
		{
			gameoverstate = GameOverOff;
			cnt = 0;
			totalcnt++;
		}
		break;
		
		case GameOverOff:
		if(cnt <= 10)
		{
			gameoverstate = GameOverOff;
			cnt++;
			totalcnt++;
		}
		else if(totalcnt >= 50)
		{
			gameoverstate = Score;
			cnt = 0;
			totalcnt = 0;
		}
		else
		{
			gameoverstate = GameOverOn;
			cnt = 0;
			totalcnt++;
		}
		break;
		
		case Score:
		if(cnt <= 80)
		{
			gameoverstate = Score;
			cnt++;
		}
		else
		{
			gameoverstate = GameOverInit;
			gameOverDone = 1;
			cnt = 0;
		}
		break;
		
		default:
		gameoverstate = GameOverInit;
		
	}
	
	switch (gameoverstate) //actions
	{
		case GameOverInit:
		break;
		
		case GameOverWait:
		break;
		
		case YouWon:
			nokia_lcd_clear();
			nokia_lcd_set_cursor(10,20);
			nokia_lcd_write_string("YOU WON!!", 1);
			nokia_lcd_render();
			break;
		
		case YouLost:
			nokia_lcd_clear();
			nokia_lcd_set_cursor(10,20);
			nokia_lcd_write_string("You Lost...", 1);
			nokia_lcd_set_cursor(15,30);
			nokia_lcd_write_string("WEAK BOY!", 1);
			nokia_lcd_render();
			break;
		
		case GameOverOn:
		nokia_lcd_clear();
		nokia_lcd_set_cursor(3,10);
		nokia_lcd_write_string("Gameover", 1);
		createHugeAlien();
		nokia_lcd_render();
		break;
		
		case GameOverOff:
		nokia_lcd_clear();
		createHugeAlien2();
		nokia_lcd_render();
		break;
		
		case Score:
		nokia_lcd_clear();
		nokia_lcd_set_cursor(10,20);
		nokia_lcd_write_string("Score: ",1);
		nokia_lcd_set_cursor(55, 20);
		nokia_lcd_write_string(snum, 1);
		nokia_lcd_render();
		break;
		
		
	}
}
//GameOver END
//----------------------------------------------------------------------------------------------


//----------------------------------------------------------------------------------------------
//Timer START
enum TimerClock{TimerInit, TimerWait, Timing, DoneTiming} timerstate;
	
void timerTick()
{
	switch(timerstate)
	{
		case TimerInit:
			timerstate = TimerWait;
			break;
			
		case TimerWait:
			if(gameStart)
			{
				timerstate = Timing;
			}
			else
			{
				timerstate = TimerWait;
			}
			break;
			
		case Timing:
			if(timerCnt <= 200)
			{
				timerstate = Timing;
				timerCnt++;
			}
			else
			{
				timerstate = DoneTiming;
			}
			break;
			
		case DoneTiming:
			timerstate = TimerWait;
			break;
			
		default:
			timerstate = TimerInit;			
	}
	
	switch (timerstate)
	{
		case TimerInit:
			break;
			
		case TimerWait:
			break;
			
		case Timing:
			break;
			
		case DoneTiming:
			snum[5];
			itoa(GameScore, snum, 10);
			gameDone = 1;
	}
}
//Timer END
//----------------------------------------------------------------------------------------------

int main(void)
{
	DDRD = 0x00;
	PORTD = 0xFF;
	nokia_lcd_init();
	nokia_lcd_clear();
	TimerSet(100);
	TimerOn();
	InitADC();
	
	while(1)
	{
		menuTick();
		playerTick();
		shootingTick();
		enemyTick();
		gameOverTick();
		timerTick();
		while(!TimerFlag);
		TimerFlag = 0;
		//player_timer++;
	}
}