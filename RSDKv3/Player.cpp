#include "RetroEngine.hpp"

Player playerList[PLAYER_COUNT];
int playerListPos     = 0;
int activePlayer      = 0;
int activePlayerCount = 1;

ushort upBuffer        = 0;
ushort downBuffer      = 0;
ushort leftBuffer      = 0;
ushort rightBuffer     = 0;
ushort jumpPressBuffer = 0;
ushort jumpHoldBuffer  = 0;

byte nextLeaderPosID = 1;
byte lastLeaderPosID = 0;

short leaderPositionBufferX[16];
short leaderPositionBufferY[16];

void ProcessPlayerControl(Player *player)
{
    switch (player->controlMode) {
        default:
        case CONTROLMODE_NORMAL:
            player->up    = keyDown.up || controller[CONT_P1].keyUp.down;
            player->down  = keyDown.down || controller[CONT_P1].keyDown.down;
            player->left  = keyDown.left || controller[CONT_P1].keyLeft.down;
            player->right = keyDown.right || controller[CONT_P1].keyRight.down;

            if (player->left && player->right) {
                player->left  = false;
                player->right = false;
            }

            player->jumpHold  = keyDown.A || keyPress.B || keyPress.C;
            player->jumpPress = keyPress.A || keyPress.B || keyPress.C;

            upBuffer <<= 1;
            upBuffer |= (byte)player->up;

            downBuffer <<= 1;
            downBuffer |= (byte)player->down;

            leftBuffer <<= 1;
            leftBuffer |= (byte)player->left;

            rightBuffer <<= 1;
            rightBuffer |= (byte)player->right;

            jumpPressBuffer <<= 1;
            jumpPressBuffer |= (byte)player->jumpPress;

            jumpHoldBuffer <<= 1;
            jumpHoldBuffer |= (byte)player->jumpHold;

            if (activePlayerCount >= 2 && !player->followPlayer1) {
                upBuffer <<= 1;
                upBuffer |= (byte)player->up;

                downBuffer <<= 1;
                downBuffer |= (byte)player->down;

                leftBuffer <<= 1;
                leftBuffer |= (byte)player->left;

                rightBuffer <<= 1;
                rightBuffer |= (byte)player->right;

                jumpPressBuffer <<= 1;
                jumpPressBuffer |= (byte)player->jumpPress;

                jumpHoldBuffer <<= 1;
                jumpHoldBuffer |= (byte)player->jumpHold;

                leaderPositionBufferX[nextLeaderPosID] = player->XPos >> 16;
                leaderPositionBufferY[nextLeaderPosID] = player->YPos >> 16;
                nextLeaderPosID                        = (nextLeaderPosID + 1) & 0xF;
                lastLeaderPosID                        = (nextLeaderPosID + 1) & 0xF;
            }
            break;

        case CONTROLMODE_NONE:
            if (activePlayerCount >= 2 && !player->followPlayer1) {
                upBuffer <<= 1;
                upBuffer |= (byte)player->up;

                downBuffer <<= 1;
                downBuffer |= (byte)player->down;

                leftBuffer <<= 1;
                leftBuffer |= (byte)player->left;

                rightBuffer <<= 1;
                rightBuffer |= (byte)player->right;

                jumpPressBuffer <<= 1;
                jumpPressBuffer |= (byte)player->jumpPress;

                jumpHoldBuffer <<= 1;
                jumpHoldBuffer |= (byte)player->jumpHold;

                leaderPositionBufferX[nextLeaderPosID] = player->XPos >> 16;
                leaderPositionBufferY[nextLeaderPosID] = player->YPos >> 16;
                nextLeaderPosID                        = (nextLeaderPosID + 1) & 0xF;
                lastLeaderPosID                        = (nextLeaderPosID + 1) & 0xF;
            }
            break;

        case CONTROLMODE_SIDEKICK:
            player->up        = upBuffer >> 15;
            player->down      = downBuffer >> 15;
            player->left      = leftBuffer >> 15;
            player->right     = rightBuffer >> 15;
            player->jumpPress = jumpPressBuffer >> 15;
            player->jumpHold  = jumpHoldBuffer >> 15;

            player->boundEntity->XPos = leaderPositionBufferX[lastLeaderPosID] << 16;
            player->boundEntity->YPos = leaderPositionBufferY[lastLeaderPosID] << 16;
            break;
    }
}
