// firmware (c) by Greg Coonrod
//
// firmware is licensed under a
// Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
//
// You should have received a copy of the license along with this
// work. If not, see <https://creativecommons.org/licenses/by-nc-sa/4.0/>.

#include "ShiftDisplayFSM.h"

uint8_t menuFieldCount(MenuState item)
{
    switch (item)
    {
    case MENU_TIME:  return 3; // hours, minutes, seconds
    case MENU_DATE:  return 3; // day, month, year
    case MENU_MODE:  return 1; // 12/24
    case MENU_ALARM: return 3; // hours, minutes, armed
    case MENU_BRIGHT: return 1; // indicator level
    case MENU_DISP: return 1;   // display level
    default:         return 0;
    }
}

void ShiftDisplayFSM::update()
{
    // Every pending value is committed here. Committing only currentState --
    // which is what this did originally -- left the menu computing transitions
    // and then discarding them, so the tree below the top level was unreachable.
    currentState = nextState;
    currentMenuState = nextMenuState;
    currentField = nextField;
}

void ShiftDisplayFSM::setState(State state)
{
    nextState = state;
}

bool ShiftDisplayFSM::takeCommit()
{
    bool pending = commitPending;
    commitPending = false;
    return pending;
}

void ShiftDisplayFSM::execute(Action action)
{
    switch (action)
    {
    case MENU_ENTER:
        if (currentState == IDLE)
        {
            nextState = MENU;
            nextMenuState = MENU_FIRST;
            nextField = 0;
        }
        break;

    case MENU_EXIT:
        // Back out one level. Leaving an editor discards whatever was being
        // edited; the caller only writes values through on a commit.
        switch (currentState)
        {
        case EDIT:
            nextState = MENU;
            nextField = 0;
            break;
        case MENU:
            nextState = IDLE;
            nextMenuState = MENU_NONE;
            nextField = 0;
            break;
        case FIRING:
            nextState = IDLE;
            break;
        default:
            break;
        }
        break;

    case MENU_UP:
        if (currentState == MENU)
        {
            nextMenuState = (currentMenuState >= MENU_LAST)
                                ? (MenuState)MENU_FIRST
                                : (MenuState)(currentMenuState + 1);
        }
        break;

    case MENU_DOWN:
        if (currentState == MENU)
        {
            nextMenuState = (currentMenuState <= MENU_FIRST)
                                ? (MenuState)MENU_LAST
                                : (MenuState)(currentMenuState - 1);
        }
        break;

    case MENU_SELECT:
        if (currentState == MENU && menuFieldCount(currentMenuState) > 0)
        {
            nextState = EDIT;
            nextField = 0;
        }
        break;

    case EDIT_NEXT:
        if (currentState == EDIT)
        {
            uint8_t last = menuFieldCount(currentMenuState);
            if (currentField + 1 >= last)
            {
                // Past the final field: commit and show the result.
                commitPending = true;
                nextState = IDLE;
                nextMenuState = MENU_NONE;
                nextField = 0;
            }
            else
            {
                nextField = currentField + 1;
            }
        }
        break;

    case MENU_TIMEOUT:
        nextState = IDLE;
        nextMenuState = MENU_NONE;
        nextField = 0;
        break;

    case ALARM_FIRE:
        nextState = FIRING;
        nextMenuState = MENU_NONE;
        nextField = 0;
        break;

    case ALARM_DISMISS:
        if (currentState == FIRING)
        {
            nextState = IDLE;
        }
        break;

    default:
        break;
    }
}
