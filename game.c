/*
 * Hocoslamfy, game code file
 * Copyright (C) 2014 Nebuleon Fumika <nebuleon@gcw-zero.com>
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include "SDL.h"
#include "SDL_image.h"

#include "main.h"
#include "init.h"
#include "platform.h"
#include "game.h"
#include "score.h"
#include "bg.h"
#include "text.h"

static uint32_t               Score;

static bool                   Boost;
static bool                   Pause;
static enum PlayerStatus      PlayerStatus;

// Where the player is. (Upper-left corner, meters.)
static float                  PlayerX;
static float                  PlayerY;
// Where the player is going. (Meters per second.)
static float                  PlayerSpeed;
// Animation frame for the player when ascending.
static uint8_t                PlayerFrame;
// Frame counter for triggering the player blinking animation.
static uint8_t                PlayerFrameBlink;
// Time the player has had the current animation frame. (In milliseconds.)
static uint32_t               PlayerFrameTime;

// Passed to the score screen after the player is done dying.
static enum GameOverReason    GameOverReason;

// What the player avoids.
static struct HocoslamfyRect* Rectangles     = NULL;
static uint32_t               RectangleCount = 0;

static float                  GenDistance;

void GameGatherInput(bool* Continue)
{
	SDL_Event ev;

	while (SDL_PollEvent(&ev))
	{
		if (IsBoostEvent(&ev) && !Pause)
			Boost = true;
		else if (IsPauseEvent(&ev) && PlayerStatus == ALIVE)
			Pause = !Pause;
		else if (IsExitGameEvent(&ev))
		{
			*Continue = false;
			return;
		}
	}
}

static void SetStatus(const enum PlayerStatus NewStatus)
{
	PlayerFrameTime = 0;
	PlayerStatus = NewStatus;
	if (NewStatus == DYING)
		PlayerSpeed = 0.0f;
}

static void AnimationControl(Uint32 Milliseconds)
{
	switch (PlayerStatus)
	{
		case ALIVE:
		case DYING:
			PlayerFrameTime = (PlayerFrameTime + Milliseconds) % (ANIMATION_TIME * ANIMATION_FRAMES);
			PlayerFrame = (PlayerFrame + (PlayerFrameTime / ANIMATION_TIME)) % ANIMATION_FRAMES;
			PlayerFrameBlink = (PlayerFrameBlink + (PlayerFrameTime / ANIMATION_TIME)) % ANIMATION_FRAMES_BLINK;
			break;

		case COLLIDED:
			PlayerFrameTime += Milliseconds;
			if (PlayerFrameTime > COLLISION_TIME)
				SetStatus(DYING);
			break;
	}
}

void GameDoLogic(bool* Continue, bool* Error, Uint32 Milliseconds)
{
	if (!Pause && PlayerStatus == ALIVE)
	{
		bool PointAwarded = false;
		uint32_t Millisecond;
		for (Millisecond = 0; Millisecond < Milliseconds; Millisecond++)
		{
			// Scroll all rectangles to the left.
			int32_t i;
			for (i = RectangleCount - 1; i >= 0; i--)
			{
				Rectangles[i].Left += FIELD_SCROLL / 1000;
				Rectangles[i].Right += FIELD_SCROLL / 1000;
				// If a rectangle is past the player, award the player with a
				// point. But there is a pair of them per column!
				if (!Rectangles[i].Passed
				 && Rectangles[i].Right < PlayerX)
				{
					Rectangles[i].Passed = true;
					if (!PointAwarded)
					{
						Score++;
						PointAwarded = true;
					}
				}
				// If a rectangle is past the left side, remove it.
				if (Rectangles[i].Right < 0.0f)
				{
					memmove(&Rectangles[i], &Rectangles[i + 1], (RectangleCount - i) * sizeof(struct HocoslamfyRect));
					RectangleCount--;
				}
			}
			// Generate a pair of rectangles now if needed.
			if (RectangleCount == 0 || FIELD_WIDTH - Rectangles[RectangleCount - 1].Right >= GenDistance)
			{
				float Left;
				if (RectangleCount == 0)
					Left = FIELD_WIDTH + FIELD_SCROLL / 1000;
				else
				{
					Left = Rectangles[RectangleCount - 1].Right + GenDistance;
					GenDistance += RECT_GEN_SPEED;
				}
				Rectangles = realloc(Rectangles, (RectangleCount + 2) * sizeof(struct HocoslamfyRect));
				RectangleCount += 2;
				Rectangles[RectangleCount - 2].Passed = Rectangles[RectangleCount - 1].Passed = false;
				Rectangles[RectangleCount - 2].Left = Rectangles[RectangleCount - 1].Left = Left;
				Rectangles[RectangleCount - 2].Right = Rectangles[RectangleCount - 1].Right = Left + RECT_WIDTH;
				// Where's the place for the player to go through?
				float GapTop = GAP_HEIGHT + (FIELD_HEIGHT / 16.0f) + ((float) rand() / (float) RAND_MAX) * (FIELD_HEIGHT - GAP_HEIGHT - (FIELD_HEIGHT / 8.0f));
				Rectangles[RectangleCount - 2].Top = FIELD_HEIGHT;
				Rectangles[RectangleCount - 2].Bottom = GapTop;
				Rectangles[RectangleCount - 1].Top = GapTop - GAP_HEIGHT;
				Rectangles[RectangleCount - 1].Bottom = 0.0f;
				Rectangles[RectangleCount - 2].Frame = rand() % 3;
				Rectangles[RectangleCount - 1].Frame = rand() % 3;
			}
			// Update the speed at which the player is going.
			PlayerSpeed += GRAVITY / 1000;
			if (Boost)
			{
				// The player expects to rise a constant amount with each press of
				// the triggering key or button, so set his or her speed to
				// boost him or her from zero, even if the speed was positive.
				// For a more physically-realistic version of thrust, use
				// [PlayerSpeed += SPEED_BOOST;].
				PlayerSpeed = SPEED_BOOST;
				Boost = false;
			}
			// Update the player's position.
			// If the player's position has collided with the borders of the field,
			// the player's game is over.
			PlayerY += PlayerSpeed / 1000;
			if (PlayerY + (PLAYER_COL_SIZE_B / 2) > FIELD_HEIGHT || PlayerY - (PLAYER_COL_SIZE_B / 2) < 0.0f)
			{
				SetStatus(COLLIDED);
				GameOverReason = FIELD_BORDER_COLLISION;
				break;
			}
			// Collision detection.
			for (i = 0; i < RectangleCount; i++)
			{
				if ((((PlayerY + (PLAYER_COL_SIZE_A / 4) > Rectangles[i].Bottom
				   && PlayerY + (PLAYER_COL_SIZE_A / 4) < Rectangles[i].Top)
				  || (PlayerY - (PLAYER_COL_SIZE_A / 4) > Rectangles[i].Bottom
				   && PlayerY - (PLAYER_COL_SIZE_A / 4) < Rectangles[i].Top))
				 && ((PlayerX - (PLAYER_COL_SIZE_A / 2) > Rectangles[i].Left
				   && PlayerX - (PLAYER_COL_SIZE_A / 2) < Rectangles[i].Right)
				  || (PlayerX + (PLAYER_COL_SIZE_A / 2) > Rectangles[i].Left
				   && PlayerX + (PLAYER_COL_SIZE_A / 2) < Rectangles[i].Right)))
				|| (((PlayerY + (PLAYER_COL_SIZE_B / 2) > Rectangles[i].Bottom
				   && PlayerY + (PLAYER_COL_SIZE_B / 2) < Rectangles[i].Top)
				  || (PlayerY - (PLAYER_COL_SIZE_B / 2) > Rectangles[i].Bottom
				   && PlayerY - (PLAYER_COL_SIZE_B / 2) < Rectangles[i].Top))
				 && ((PlayerX - (PLAYER_COL_SIZE_B / 4) > Rectangles[i].Left
				   && PlayerX - (PLAYER_COL_SIZE_B / 4) < Rectangles[i].Right)
				  || (PlayerX + (PLAYER_COL_SIZE_B / 4) > Rectangles[i].Left
				   && PlayerX + (PLAYER_COL_SIZE_B / 4) < Rectangles[i].Right))))
				{
					SetStatus(COLLIDED);
					GameOverReason = RECTANGLE_COLLISION;
					break;
				}
			}
		}

		AdvanceBackground(Milliseconds);
	}
	else if (PlayerStatus == DYING)
	{
		uint32_t Millisecond;
		for (Millisecond = 0; Millisecond < Milliseconds; Millisecond++)
		{
			// Update the speed at which the player is going.
			PlayerSpeed += GRAVITY / 1000;
			// Update the player's position.
			// If the player's position has reached the bottom of the screen,
			// send him or her to the score screen.
			PlayerY += PlayerSpeed / 1000;
			if (PlayerY < 0.0f)
			{
				ToScore(Score, GameOverReason);
				return;
			}
		}
	}

	AnimationControl(Milliseconds);
}

void GameOutputFrame()
{
	// Draw the background.
	DrawBackground();

	// Draw the rectangles.
	uint32_t i;
	for (i = 0; i < RectangleCount; i++)
	{
		SDL_Rect ColumnDestRect = {
			.x = (int) (Rectangles[i].Left * SCREEN_WIDTH / FIELD_WIDTH) - 20,
			.y = SCREEN_HEIGHT - (int) (Rectangles[i].Top * SCREEN_HEIGHT / FIELD_HEIGHT),
			.w = (int) ((Rectangles[i].Right - Rectangles[i].Left) * SCREEN_WIDTH / FIELD_WIDTH) + 40,
			.h = (int) ((Rectangles[i].Top - Rectangles[i].Bottom) * SCREEN_HEIGHT / FIELD_HEIGHT)
		};
		SDL_Rect ColumnSourceRect = { .x = 0, .y = 0, .w = ColumnDestRect.w, .h = ColumnDestRect.h };
		// Odd-numbered rectangle indices are at the bottom of the field,
		// so start their column image from the top.
		if (i & 1) {
			ColumnSourceRect.y = 0;
		} else {
			ColumnSourceRect.y = 480 - ColumnDestRect.h;
		}
		ColumnSourceRect.x = 64 * Rectangles[i].Frame;
		SDL_BlitSurface(ColumnImage, &ColumnSourceRect, Screen, &ColumnDestRect);
	}

	// Draw the character.
	SDL_Rect PlayerDestRect = {
		.x = (int) (PlayerX * SCREEN_WIDTH / FIELD_WIDTH) - (PLAYER_FRAME_SIZE / 2) -1,
		.y = (int) (SCREEN_HEIGHT - (PlayerY * SCREEN_HEIGHT / FIELD_HEIGHT)) - (PLAYER_FRAME_SIZE / 2),
		.w = (int) PLAYER_FRAME_SIZE,
		.h = (int) PLAYER_FRAME_SIZE
	};
	SDL_Rect PlayerSourceRect = {
		.x = 0,
		.y = 0,
		.w = 32,
		.h = 32
	};
#ifdef DRAW_BEE_COLLISION
	SDL_Rect PlayerPixelsA = {
		.x = (int) ((PlayerX - (PLAYER_COL_SIZE_A / 2)) * SCREEN_WIDTH / FIELD_WIDTH),
		.y = (int) (SCREEN_HEIGHT - ((PlayerY + (PLAYER_COL_SIZE_A / 4)) * SCREEN_HEIGHT / FIELD_HEIGHT)),
		.w = (int) (PLAYER_COL_SIZE_A * SCREEN_HEIGHT / FIELD_HEIGHT),
		.h = (int) ((PLAYER_COL_SIZE_A / 2) * SCREEN_HEIGHT / FIELD_HEIGHT)
	};
	SDL_Rect PlayerPixelsB = {
		.x = (int) ((PlayerX - (PLAYER_COL_SIZE_B / 4)) * SCREEN_WIDTH / FIELD_WIDTH),
		.y = (int) (SCREEN_HEIGHT - ((PlayerY + (PLAYER_COL_SIZE_B / 2)) * SCREEN_HEIGHT / FIELD_HEIGHT)),
		.w = (int) ((PLAYER_COL_SIZE_B / 2) * SCREEN_HEIGHT / FIELD_HEIGHT),
		.h = (int) (PLAYER_COL_SIZE_B * SCREEN_HEIGHT / FIELD_HEIGHT)
	};
#endif
	switch (PlayerStatus)
	{
		case ALIVE:
			if (PlayerSpeed > -2.0f) {
				PlayerSourceRect.x = 32 * PlayerFrame;
				if (PlayerFrameBlink > 92)
					PlayerSourceRect.x = 64 + 32 * PlayerFrame;
			} else {
				PlayerSourceRect.x = 128 + 32 * PlayerFrame;
				if (PlayerFrameBlink > 92)
					PlayerSourceRect.x = 192 + 32 * PlayerFrame;
			}
			SDL_BlitSurface(CharacterFrames, &PlayerSourceRect, Screen, &PlayerDestRect);
#ifdef DRAW_BEE_COLLISION
			SDL_FillRect(Screen, &PlayerPixelsA, SDL_MapRGB(Screen->format, 255, 255, 255));
			SDL_FillRect(Screen, &PlayerPixelsB, SDL_MapRGB(Screen->format, 255, 255, 255));
#endif
			break;

		case COLLIDED:
			PlayerSourceRect.w = 48;
			PlayerSourceRect.h = 48;
			PlayerDestRect.x -= 8;
			PlayerDestRect.y -= 8;
			PlayerDestRect.w += 16;
			PlayerDestRect.h += 16;
			SDL_BlitSurface(CollisionImage, &PlayerSourceRect, Screen, &PlayerDestRect);
			break;

		case DYING:
			PlayerSourceRect.x = 256 + 32 * PlayerFrame;
			SDL_BlitSurface(CharacterFrames, &PlayerSourceRect, Screen, &PlayerDestRect);
			break;
	}

	// Draw the player's current score.
	char ScoreString[17];
	sprintf(ScoreString, "Score%10" PRIu32, Score);
	if (SDL_MUSTLOCK(Screen))
		SDL_LockSurface(Screen);
	PrintStringOutline32(ScoreString,
		SDL_MapRGB(Screen->format, 255, 255, 255),
		SDL_MapRGB(Screen->format, 0, 0, 0),
		Screen->pixels,
		Screen->pitch,
		0,
		0,
		SCREEN_WIDTH,
		SCREEN_HEIGHT,
		RIGHT,
		TOP);
	if (SDL_MUSTLOCK(Screen))
		SDL_UnlockSurface(Screen);

	SDL_Flip(Screen);
}

void ToGame(void)
{
	Score = 0;
	Boost = false;
	Pause = false;
	SetStatus(ALIVE);
	PlayerX = FIELD_WIDTH / 4;
	PlayerY = (FIELD_HEIGHT - PLAYER_COL_SIZE_B) / 2;
	PlayerSpeed = 0.0f;
	if (Rectangles != NULL)
	{
		free(Rectangles);
		Rectangles = NULL;
	}
	RectangleCount = 0;
	GenDistance = RECT_GEN_START;

	GatherInput = GameGatherInput;
	DoLogic     = GameDoLogic;
	OutputFrame = GameOutputFrame;
}
