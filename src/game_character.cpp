/*
 * This file is part of EasyRPG Player.
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * EasyRPG Player is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with EasyRPG Player. If not, see <http://www.gnu.org/licenses/>.
 */

// Headers
#include "audio.h"
#include "game_character.h"
#include "game_map.h"
#include "game_player.h"
#include "game_switches.h"
#include "game_system.h"
#include "input.h"
#include "main_data.h"
#include "game_message.h"
#include "player.h"
#include "util_macro.h"
#include <cmath>
#include <cassert>
#include <cstdlib>

Game_Character::Game_Character() :
	tile_id(0),
	pattern(RPG::EventPage::Frame_middle),
	original_pattern(RPG::EventPage::Frame_middle),
	last_pattern(0),
	through(false),
	animation_id(0),
	animation_type(RPG::EventPage::AnimType_non_continuous),
	original_move_frequency(-1),
	move_type(RPG::EventPage::MoveType_stationary),
	move_failed(false),
	remaining_step(0),
	move_count(0),
	wait_count(0),
	jumping(false),
	jump_x(0),
	jump_y(0),
	jump_plus_x(0),
	jump_plus_y(0),
	anime_count(0),
	stop_count(0),
	max_stop_count(0),
	walk_animation(true),
	cycle_stat(false),
	opacity(255),
	visible(true) {
}

Game_Character::~Game_Character() {
	Game_Map::RemovePendingMove(this);
}

int Game_Character::GetSteppingSpeed() const {
	int move_speed = GetMoveSpeed();
	if (IsSpinning()) {
		// 24, 16, 12, 8, 6, 4
		return (move_speed < 4) ? 48 / (move_speed + 1) : 24 / (move_speed - 1);
	} else if (IsMoving()) {
		// 12, 10, 8, 6, 5, 4
		return (move_speed < 4) ? 60 / (move_speed + 4) : 30 / (move_speed + 1);
	} else {
		// 16, 12, 10, 8, 7, 6
		return (move_speed < 2) ? 16 : 60 / (move_speed + 3);
	}
}

bool Game_Character::IsMoving() const {
	return !IsJumping() && remaining_step > 0;
}

bool Game_Character::IsJumping() const {
	return jumping;
}

bool Game_Character::IsStopping() const {
	return !(IsMoving() || IsJumping());
}

bool Game_Character::IsPassable(int x, int y, int d) const {
	if (d > 3) {
		int dx = (d == UpRight || d == DownRight) - (d == DownLeft || d == UpLeft);
		int dy = (d == DownRight || d == DownLeft) - (d == UpRight || d == UpLeft);
		return ((IsPassable(x, y, -dx + 2) && IsPassable(x + dx, y, dy + 1)) ||
			(IsPassable(x, y, dy + 1) && IsPassable(x, y + dy, -dx + 2)));
	}

	int new_x = Game_Map::RoundX(x + (d == Right ? 1 : d == Left ? -1 : 0));
	int new_y = Game_Map::RoundY(y + (d == Down ? 1 : d == Up ? -1 : 0));

	if (!Game_Map::IsValid(new_x, new_y))
		return false;

	if (GetThrough()) return true;

	if (!Game_Map::IsPassable(x, y, d, this))
		return false;

	if (!Game_Map::IsPassable(new_x, new_y, (d + 2) % 4, this))
		return false;

	if (Main_Data::game_player->IsInPosition(new_x, new_y)
		&& !Main_Data::game_player->GetThrough() && !GetSpriteName().empty()
		&& GetLayer() == RPG::EventPage::Layers_same) {
			return false;
	}

	return true;
}

bool Game_Character::IsLandable(int x, int y) const
{
	if (!Game_Map::IsValid(x, y))
		return false;

	if (GetThrough()) return true;

	if (!Game_Map::IsLandable(x, y, this))
		return false;

	if (Main_Data::game_player->IsInPosition(x, y)) {
		if (!Main_Data::game_player->GetThrough() && !GetSpriteName().empty() && (this != Main_Data::game_player.get())) {
			return false;
		}
	}

	return true;
}

bool Game_Character::IsMessageBlocking() const {
	return Game_Message::message_waiting && !Game_Message::GetContinueEvents();
}

void Game_Character::MoveTo(int x, int y) {
	SetX(x % Game_Map::GetWidth());
	SetY(y % Game_Map::GetHeight());
}

int Game_Character::GetScreenX() const {
	int x = GetRealX() / TILE_SIZE - Game_Map::GetDisplayX() / TILE_SIZE + (TILE_SIZE / 2);

	if (Game_Map::LoopHorizontal()) {
		int map_width = Game_Map::GetWidth() * TILE_SIZE;
		x = (x + map_width) % map_width;
	}

	return x;
}

int Game_Character::GetScreenY() const {
	int y = GetRealY() / TILE_SIZE - Game_Map::GetDisplayY() / TILE_SIZE + TILE_SIZE;

	if (Game_Map::LoopVertical()) {
		int map_height = Game_Map::GetHeight() * TILE_SIZE;
		y = (y + map_height) % map_height;
	}

	if (IsJumping()) {
		int jump_height = (remaining_step > SCREEN_TILE_WIDTH / 2 ? SCREEN_TILE_WIDTH - remaining_step : remaining_step) / 8;
		y -= (jump_height < 5 ? jump_height * 2 : jump_height < 13 ? jump_height + 4 : 16);
	}

	return y;
}

int Game_Character::GetScreenZ() const {
	int z = (GetRealY() - Game_Map::GetDisplayY() + 3) / TILE_SIZE + (SCREEN_TILE_WIDTH / TILE_SIZE);

	int max_height = Game_Map::GetHeight() * TILE_SIZE;

	// wrap on map boundaries
	if (z < 0) {
		z += max_height;
	}

	if (GetLayer() == RPG::EventPage::Layers_below) {
		z -= TILE_SIZE;
	}
	if (GetLayer() == RPG::EventPage::Layers_above) {
		z += TILE_SIZE;
	}

	// Prevent underflow (not rendered in this case)
	// ToDo: It's probably the best to rework the z-layer part of the tilemap code
	if (z < 1) {
		z = 1;
	}

	// 1 less to correctly render compared to some tile map tiles (star tiles e.g.)
	return z - 1;
}

void Game_Character::Update() {
	if (IsJumping()) {
		UpdateJump();
		anime_count += (IsSpinning() ? 1 : 0);
	} else if (IsContinuous() || IsSpinning()) {
		UpdateMove();
		UpdateStop();
	} else {
		if (IsMoving()) {
			UpdateMove();
		} else {
			UpdateStop();
		}
	}

	if (anime_count >= GetSteppingSpeed()) {
		if (IsSpinning()) {
			SetSpriteDirection((GetSpriteDirection() + 1) % 4);
		} else if (!IsContinuous() && IsStopping()) {
			pattern = original_pattern;
			last_pattern = last_pattern == RPG::EventPage::Frame_left ? RPG::EventPage::Frame_right : RPG::EventPage::Frame_left;
		} else {
			if (last_pattern == RPG::EventPage::Frame_left) {
				if (pattern == RPG::EventPage::Frame_right) {
					pattern = RPG::EventPage::Frame_middle;
					last_pattern = RPG::EventPage::Frame_right;
				} else {
					pattern = RPG::EventPage::Frame_right;
				}
			} else {
				if (pattern == RPG::EventPage::Frame_left) {
					pattern = RPG::EventPage::Frame_middle;
					last_pattern = RPG::EventPage::Frame_left;
				} else {
					pattern = RPG::EventPage::Frame_left;
				}
			}
		}

		anime_count = 0;
	}

	if (wait_count > 0) {
		wait_count -= 1;
		return;
	}

	if (stop_count >= max_stop_count) {
		if (IsMoveRouteOverwritten()) {
			MoveTypeCustom();
		} else if (!IsMessageBlocking() && !Game_Map::GetInterpreter().IsRunning()) {
			UpdateSelfMovement();
		}
	}
}

void Game_Character::UpdateMove() {
	if (remaining_step > 0)
		remaining_step -= pow(2.0, 1 + GetMoveSpeed());

	anime_count +=
		(animation_type != RPG::EventPage::AnimType_fixed_graphic && walk_animation) ? 1 : 0;
}

void Game_Character::UpdateJump() {
	// 8, 12, 16, 24, 32, 64
	int move_speed = GetMoveSpeed();
	remaining_step -= move_speed < 5 ? 48 / (2 + pow(2.0, 3 - move_speed)) : 64 / (7 - move_speed);
	if (remaining_step <= 0)
		jumping = false;
}

void Game_Character::UpdateSelfMovement() {
	switch (move_type) {
	case RPG::EventPage::MoveType_random:
		MoveTypeRandom();
		break;
	case RPG::EventPage::MoveType_vertical:
		MoveTypeCycleUpDown();
		break;
	case RPG::EventPage::MoveType_horizontal:
		MoveTypeCycleLeftRight();
		break;
	case RPG::EventPage::MoveType_toward:
		MoveTypeTowardsPlayer();
		break;
	case RPG::EventPage::MoveType_away:
		MoveTypeAwayFromPlayer();
		break;
	case RPG::EventPage::MoveType_custom:
		MoveTypeCustom();
		break;
	}
}

void Game_Character::UpdateStop() {
	if (pattern != original_pattern && !IsContinuous())
		anime_count++;
	stop_count++;
}

void Game_Character::MoveTypeRandom() {
	if (IsStopping()) {
		switch (rand() % 6) {
		case 0:
			stop_count = 0;
			break;
		case 1: case 2:
			MoveRandom();
			break;
		default:
			MoveForward();
		}
	}
}

void Game_Character::MoveTypeCycleLeftRight() {
	if (IsStopping()) {
		Move(cycle_stat ? Left : Right);

		if (move_failed) {
			Wait();
			stop_count = 0;
			// TODO: After waiting, try once more in the same direction
			cycle_stat = move_failed ? !cycle_stat : cycle_stat;
		}
	}
}

void Game_Character::MoveTypeCycleUpDown() {
	if (IsStopping()) {
		Move(cycle_stat ? Up : Down);

		if (move_failed) {
			Wait();
			stop_count = 0;
			// TODO: After waiting, try once more in the same direction
			cycle_stat = !cycle_stat;
		}
	}
}

void Game_Character::MoveTypeTowardsPlayer() {
	if (IsStopping()) {
		int sx = GetX() - Main_Data::game_player->GetX();
		int sy = GetY() - Main_Data::game_player->GetY();

		if ( std::abs(sx) + std::abs(sy) >= 20 ) {
			MoveRandom();
		} else {
			switch (rand() % 6) {
			case 0:
				MoveRandom();
				break;
			case 1:
				MoveForward();
				break;
			default:
				MoveTowardsPlayer();
			}
		}
	}
}

void Game_Character::MoveTypeAwayFromPlayer() {
	if (IsStopping()) {
		int sx = GetX() - Main_Data::game_player->GetX();
		int sy = GetY() - Main_Data::game_player->GetY();

		if ( std::abs(sx) + std::abs(sy) >= 20 ) {
			MoveRandom();
		} else {
			switch (rand() % 6) {
			case 0:
				MoveRandom();
				break;
			case 1:
				MoveForward();
				break;
			default:
				MoveAwayFromPlayer();
			}
		}
	}
}

void Game_Character::MoveTypeCustom() {
	// Detect if custom movement or event overwrite
	const RPG::MoveRoute* active_route;
	int active_route_index;
	bool overwrite_changed = IsMoveRouteOverwritten();
	if (IsMoveRouteOverwritten()) {
		active_route = &GetMoveRoute();
		active_route_index = GetMoveRouteIndex();
	}
	else {
		active_route = &original_move_route;
		active_route_index = GetOriginalMoveRouteIndex();
	}

	if (IsStopping()) {
		move_failed = false;
		if ((size_t)active_route_index >= active_route->move_commands.size()) {
			// End of Move list
			if (active_route->repeat) {
				active_route_index = 0;
				SetMoveRouteRepeated(true);
			} else if (IsMoveRouteOverwritten()) {
				CancelMoveRoute();
				Game_Map::RemovePendingMove(this);
				stop_count = 0;
			}
		} else {
			for (; (size_t)active_route_index < active_route->move_commands.size(); ++active_route_index) {
				if (!IsStopping() || wait_count > 0 || stop_count < max_stop_count)
					break;

				const RPG::MoveCommand& move_command = active_route->move_commands[active_route_index];

				switch (move_command.command_id) {
				case RPG::MoveCommand::Code::move_up:
				case RPG::MoveCommand::Code::move_right:
				case RPG::MoveCommand::Code::move_down:
				case RPG::MoveCommand::Code::move_left:
				case RPG::MoveCommand::Code::move_upright:
				case RPG::MoveCommand::Code::move_downright:
				case RPG::MoveCommand::Code::move_downleft:
				case RPG::MoveCommand::Code::move_upleft:
					Move(move_command.command_id);
					break;
				case RPG::MoveCommand::Code::move_random:
					MoveRandom();
					break;
				case RPG::MoveCommand::Code::move_towards_hero:
					MoveTowardsPlayer();
					break;
				case RPG::MoveCommand::Code::move_away_from_hero:
					MoveAwayFromPlayer();
					break;
				case RPG::MoveCommand::Code::move_forward:
					MoveForward();
					break;
				case RPG::MoveCommand::Code::face_up:
					Turn(Up);
					break;
				case RPG::MoveCommand::Code::face_right:
					Turn(Right);
					break;
				case RPG::MoveCommand::Code::face_down:
					Turn(Down);
					break;
				case RPG::MoveCommand::Code::face_left:
					Turn(Left);
					break;
				case RPG::MoveCommand::Code::turn_90_degree_right:
					Turn90DegreeRight();
					break;
				case RPG::MoveCommand::Code::turn_90_degree_left:
					Turn90DegreeLeft();
					break;
				case RPG::MoveCommand::Code::turn_180_degree:
					Turn180Degree();
					break;
				case RPG::MoveCommand::Code::turn_90_degree_random:
					Turn90DegreeLeftOrRight();
					break;
				case RPG::MoveCommand::Code::face_random_direction:
					FaceRandomDirection();
					break;
				case RPG::MoveCommand::Code::face_hero:
					TurnTowardHero();
					break;
				case RPG::MoveCommand::Code::face_away_from_hero:
					TurnAwayFromHero();
					break;
				case RPG::MoveCommand::Code::wait:
					Wait();
					break;
				case RPG::MoveCommand::Code::begin_jump:
					BeginJump(active_route, &active_route_index);
					break;
				case RPG::MoveCommand::Code::end_jump:
					// EndJump();
					break;
				case RPG::MoveCommand::Code::lock_facing:
					SetFacingLocked(true);
					break;
				case RPG::MoveCommand::Code::unlock_facing:
					SetFacingLocked(false);
					break;
				case RPG::MoveCommand::Code::increase_movement_speed:
					SetMoveSpeed(min(GetMoveSpeed() + 1, 6));
					break;
				case RPG::MoveCommand::Code::decrease_movement_speed:
					SetMoveSpeed(max(GetMoveSpeed() - 1, 1));
					break;
				case RPG::MoveCommand::Code::increase_movement_frequence:
					SetMoveFrequency(min(GetMoveFrequency() + 1, 8));
					break;
				case RPG::MoveCommand::Code::decrease_movement_frequence:
					SetMoveFrequency(max(GetMoveFrequency() - 1, 1));
					break;
				case RPG::MoveCommand::Code::switch_on: // Parameter A: Switch to turn on
					Game_Switches[move_command.parameter_a] = true;
					Game_Map::SetNeedRefresh(true);
					break;
				case RPG::MoveCommand::Code::switch_off: // Parameter A: Switch to turn off
					Game_Switches[move_command.parameter_a] = false;
					Game_Map::SetNeedRefresh(true);
					break;
				case RPG::MoveCommand::Code::change_graphic: // String: File, Parameter A: index
					SetGraphic(move_command.parameter_string, move_command.parameter_a);
					break;
				case RPG::MoveCommand::Code::play_sound_effect: // String: File, Parameters: Volume, Tempo, Balance
					if (move_command.parameter_string != "(OFF)" && move_command.parameter_string != "(Brak)") {
						Audio().SE_Play(move_command.parameter_string,
							move_command.parameter_a, move_command.parameter_b);
					}
					break;
				case RPG::MoveCommand::Code::walk_everywhere_on:
					through = true;
					break;
				case RPG::MoveCommand::Code::walk_everywhere_off:
					through = false;
					break;
				case RPG::MoveCommand::Code::stop_animation:
					walk_animation = false;
					break;
				case RPG::MoveCommand::Code::start_animation:
					walk_animation = true;
					break;
				case RPG::MoveCommand::Code::increase_transp:
					SetOpacity(max(40, GetOpacity() - 45));
					break;
				case RPG::MoveCommand::Code::decrease_transp:
					SetOpacity(GetOpacity() + 45);
					break;
				}

				if (move_failed) {
					if (active_route->skippable)
						++active_route_index;
					break;
				}
			}
		}
	}

	// When the overwrite status changed the active_index belongs to the
	// current non-active move route
	if (overwrite_changed != IsMoveRouteOverwritten()) {
		if (IsMoveRouteOverwritten()) {
			SetOriginalMoveRouteIndex(active_route_index);
		}
		else {
			SetMoveRouteIndex(active_route_index);
		}
	}
	else {
		if (IsMoveRouteOverwritten()) {
			SetMoveRouteIndex(active_route_index);
		}
		else {
			SetOriginalMoveRouteIndex(active_route_index);
		}
	}
}

void Game_Character::Move(int dir) {
	int dx = (dir == Right || dir == UpRight || dir == DownRight) - (dir == Left || dir == DownLeft || dir == UpLeft);
	int dy = (dir == Down || dir == DownRight || dir == DownLeft) - (dir == Up || dir == UpRight || dir == UpLeft);

	SetDirection(dir);
	if (!IsDirectionFixed()) {
		if (dir > 3) // Diagonal
			SetSpriteDirection(GetSpriteDirection() % 2 ? -dx + 2 : dy + 1);
		else
			SetSpriteDirection(dir);
	}

	if (jumping) {
		jump_plus_x += dx;
		jump_plus_y += dy;
		return;
	}

	move_failed = !IsPassable(GetX(), GetY(), dir);
	if (move_failed) {
		if (!CheckEventTriggerTouch(Game_Map::RoundX(GetX() + dx), Game_Map::RoundY(GetY() + dy)))
			return;
	} else {
		SetX(Game_Map::RoundX(GetX() + dx));
		SetY(Game_Map::RoundY(GetY() + dy));
		remaining_step = SCREEN_TILE_WIDTH;
		BeginMove();
	}

	stop_count = 0;
	max_stop_count = (GetMoveFrequency() > 7) ? 0 : pow(2.0, 9 - GetMoveFrequency());
}

void Game_Character::MoveForward() {
	Move(GetDirection());
}

void Game_Character::MoveRandom() {
	Move(rand() % 4);
}

void Game_Character::MoveTowardsPlayer() {
	int sx = DistanceXfromPlayer();
	int sy = DistanceYfromPlayer();

	if (sx != 0 || sy != 0) {
		if ( std::abs(sx) > std::abs(sy) ) {
			Move((sx > 0) ? Left : Right);
			if (move_failed && sy != 0)
				Move((sy > 0) ? Up : Down);
		} else {
			Move((sy > 0) ? Up : Down);
			if (move_failed && sx != 0) {
				Move((sx > 0) ? Left : Right);
			}
		}
	}
}

void Game_Character::MoveAwayFromPlayer() {
	int sx = DistanceXfromPlayer();
	int sy = DistanceYfromPlayer();

	if (sx != 0 || sy != 0) {
		if ( std::abs(sx) > std::abs(sy) ) {
			Move((sx > 0) ? Right : Left);
			if (move_failed && sy != 0)
				Move((sy > 0) ? Down : Up);
		} else {
			Move((sy > 0) ? Down : Up);
			if (move_failed && sx != 0)
				Move((sx > 0) ? Right : Left);
		}
	}
}

void Game_Character::Turn(int dir) {
	SetDirection(dir);
	SetSpriteDirection(dir);
	move_failed = false;
	stop_count = 0;
	max_stop_count = (GetMoveFrequency() > 7) ? 0 : pow(2.0, 8 - GetMoveFrequency());
}

void Game_Character::Turn90DegreeLeft() {
	Turn((GetSpriteDirection() + 3) % 4);
}

void Game_Character::Turn90DegreeRight() {
	Turn((GetSpriteDirection() + 1) % 4);
}

void Game_Character::Turn180Degree() {
	Turn((GetSpriteDirection() + 2) % 4);
}

void Game_Character::Turn90DegreeLeftOrRight() {
	int value = rand() % 2;

	if (value == 0) {
		Turn90DegreeLeft();
	} else {
		Turn90DegreeRight();
	}
}


void Game_Character::BeginMove() {
	// no-op
}

void Game_Character::TurnTowardHero() {
	int sx = DistanceXfromPlayer();
	int sy = DistanceYfromPlayer();

	if ( std::abs(sx) > std::abs(sy) ) {
		Turn((sx > 0) ? Left : Right);
	}
	else if ( std::abs(sx) < std::abs(sy) ) {
		Turn((sy > 0) ? Up : Down);
	}
}

void Game_Character::TurnAwayFromHero() {
	int sx = DistanceXfromPlayer();
	int sy = DistanceYfromPlayer();

	if ( std::abs(sx) > std::abs(sy) ) {
		Turn((sx > 0) ? Right : Left);
	}
	else if ( std::abs(sx) < std::abs(sy) ) {
		Turn((sy > 0) ? Down : Up);
	}
}

void Game_Character::FaceRandomDirection() {
	Turn(rand() % 4);
}

void Game_Character::Wait() {
	wait_count += 20;
}

void Game_Character::BeginJump(const RPG::MoveRoute* current_route, int* current_index) {
	jump_x = GetX();
	jump_y = GetY();
	jump_plus_x = 0;
	jump_plus_y = 0;
	jumping = true;

	bool end_found = false;
	unsigned int i;
	for (i = *current_index; i < current_route->move_commands.size(); ++i) {
		const RPG::MoveCommand& move_command = current_route->move_commands[i];
		switch (move_command.command_id) {
			case RPG::MoveCommand::Code::move_up:
			case RPG::MoveCommand::Code::move_right:
			case RPG::MoveCommand::Code::move_down:
			case RPG::MoveCommand::Code::move_left:
			case RPG::MoveCommand::Code::move_upright:
			case RPG::MoveCommand::Code::move_downright:
			case RPG::MoveCommand::Code::move_downleft:
			case RPG::MoveCommand::Code::move_upleft:
				Move(move_command.command_id);
				break;
			case RPG::MoveCommand::Code::move_random:
				MoveRandom();
				break;
			case RPG::MoveCommand::Code::move_towards_hero:
				MoveTowardsPlayer();
				break;
			case RPG::MoveCommand::Code::move_away_from_hero:
				MoveAwayFromPlayer();
				break;
			case RPG::MoveCommand::Code::move_forward:
				MoveForward();
				break;
			default:
				break;
		}

		if (move_command.command_id == RPG::MoveCommand::Code::end_jump) {
			end_found = true;
			break;
		}
	}

	if (!end_found) {
		// No EndJump found. Move route ends directly
		*current_index = i;
		jumping = false;
		return;
	}

	int new_x = jump_x + jump_plus_x;
	int new_y = jump_y + jump_plus_y;

	if (Game_Map::LoopHorizontal()) {
		int map_width = Game_Map::GetWidth();
		if (new_x < 0) {
			jump_x += map_width;
			new_x += map_width;
		} else if (new_x >= map_width) {
			jump_x -= map_width;
			new_x -= map_width;
		}
	}

	if (Game_Map::LoopVertical()) {
		int map_height = Game_Map::GetHeight();
		if (new_y < 0) {
			jump_y += map_height;
			new_y += map_height;
		} else if (new_y >= map_height) {
			jump_y -= map_height;
			new_y -= map_height;
		}
	}

	if (
		// A character can always land on a tile they were already standing on
		!(jump_plus_x == 0 && jump_plus_y == 0) &&
		!IsLandable(new_x, new_y)
	) {
		// Reset to begin jump command and try again...
		move_failed = true;
		jumping = false;

		if (current_route->skippable) {
			*current_index = i;
			return;
		}

		return;
	}

	SetX(new_x);
	SetY(new_y);
	*current_index = i;

	remaining_step = SCREEN_TILE_WIDTH;
	stop_count = 0;
	max_stop_count = (GetMoveFrequency() > 7) ? 0 : pow(2.0, 9 - GetMoveFrequency());
	move_failed = false;
}

void Game_Character::EndJump() {
	// no-op
}

int Game_Character::DistanceXfromPlayer() const {
	int sx = GetX() - Main_Data::game_player->GetX();
	if (Game_Map::LoopHorizontal()) {
		if (std::abs(sx) > Game_Map::GetWidth() / 2) {
			sx -= Game_Map::GetWidth();
		}
	}
	return sx;
}

int Game_Character::DistanceYfromPlayer() const {
	int sy = GetY() - Main_Data::game_player->GetY();
	if (Game_Map::LoopVertical()) {
		if (std::abs(sy) > Game_Map::GetHeight() / 2) {
			sy -= Game_Map::GetHeight();
		}
	}
	return sy;
}

void Game_Character::ForceMoveRoute(RPG::MoveRoute* new_route,
									int frequency) {

	Game_Map::RemovePendingMove(this);
	Game_Map::AddPendingMove(this);

	original_move_frequency = GetMoveFrequency();

	SetMoveRoute(*new_route);
	SetMoveRouteIndex(0);
	SetMoveRouteOverwritten(true);
	SetMoveRouteRepeated(false);
	SetMoveFrequency(frequency);
	wait_count = 0;
	max_stop_count = 0;
}

void Game_Character::CancelMoveRoute() {
	SetMoveRouteOverwritten(false);
	SetMoveFrequency(original_move_frequency);
}

int Game_Character::GetTileId() const {
	return tile_id;
}

int Game_Character::GetRealX() const {
	int x = GetX() * SCREEN_TILE_WIDTH;

	if (IsMoving()) {
		int d = GetDirection();
		if (d == Right || d == UpRight || d == DownRight)
			x -= remaining_step;
		else if (d == Left || d == UpLeft || d == DownLeft)
			x += remaining_step;
	} else if (IsJumping())
		x -= ((GetX() - jump_x) * remaining_step);

	return x;
}

int Game_Character::GetRealY() const {
	int y = GetY() * SCREEN_TILE_WIDTH;

	if (IsMoving()) {
		int d = GetDirection();
		if (d == Down || d == DownRight || d == DownLeft)
			y -= remaining_step;
		else if (d == Up || d == UpRight || d == UpLeft)
			y += remaining_step;
	} else if (IsJumping())
		y -= (GetY() - jump_y) * remaining_step;

	return y;
}

int Game_Character::GetRemainingStep() const {
	return remaining_step;
}

int Game_Character::GetPattern() const {
	return pattern;
}

bool Game_Character::GetThrough() const {
	return through;
}

int Game_Character::GetAnimationId() const {
	return animation_id;
}
void Game_Character::SetAnimationId(int new_animation_id) {
	animation_id = new_animation_id;
}

bool Game_Character::IsInPosition(int x, int y) const {
	return ((GetX() == x) && (GetY() == y));
}

void Game_Character::SetOpacity(int opacity) {
	this->opacity = min(max(0, opacity), 255);
}

int Game_Character::GetOpacity() const {
	return opacity;
}

bool Game_Character::GetVisible() const {
	return visible;
}

void Game_Character::SetVisible(bool visible) {
	this->visible = visible;
}

bool Game_Character::IsFlashPending() const {
	return GetFlashTimeLeft() > 0;
}

bool Game_Character::IsDirectionFixed() const {
	return
		animation_type == RPG::EventPage::AnimType_fixed_continuous ||
		animation_type == RPG::EventPage::AnimType_fixed_graphic ||
		animation_type == RPG::EventPage::AnimType_fixed_non_continuous ||
		IsFacingLocked();
}

bool Game_Character::IsContinuous() const {
	return
		animation_type == RPG::EventPage::AnimType_continuous ||
		animation_type == RPG::EventPage::AnimType_fixed_continuous;
}

bool Game_Character::IsSpinning() const {
	return animation_type == RPG::EventPage::AnimType_spin;
}

int Game_Character::GetBushDepth() {
	return Game_Map::GetBushDepth(GetX(), GetY());
}

void Game_Character::SetGraphic(const std::string& name, int index) {
	SetSpriteName(name);
	SetSpriteIndex(index);
	pattern = RPG::EventPage::Frame_middle;
}

// Gets Character
Game_Character* Game_Character::GetCharacter(int character_id, int event_id) {
	tEventHash::const_iterator it;

	switch (character_id) {
		case CharPlayer:
			// Player/Hero
			return Main_Data::game_player.get();
		case CharBoat:
			return Game_Map::GetVehicle(Game_Vehicle::Boat);
		case CharShip:
			return Game_Map::GetVehicle(Game_Vehicle::Ship);
		case CharAirship:
			return Game_Map::GetVehicle(Game_Vehicle::Airship);
		case CharThisEvent:
			// This event
			it = Game_Map::GetEvents().find(event_id);

			if (it == Game_Map::GetEvents().end()) {
				return NULL;
			}

			return it->second.get();
		default:
			// Other events
			it = Game_Map::GetEvents().find(character_id);

			if (it == Game_Map::GetEvents().end()) {
				return NULL;
			}

			return it->second.get();
	}
}
