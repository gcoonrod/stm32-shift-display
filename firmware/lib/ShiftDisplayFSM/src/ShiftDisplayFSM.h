// firmware (c) by Greg Coonrod
//
// firmware is licensed under a
// Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
//
// You should have received a copy of the license along with this
// work. If not, see <https://creativecommons.org/licenses/by-nc-sa/4.0/>.

#ifndef _SHIFTDISPLAYFSM_H
#define _SHIFTDISPLAYFSM_H

#include <inttypes.h>

enum State {
    IDLE,   // showing the clock
    MENU,   // scrolling the top-level entries
    EDIT,   // editing a field of the selected entry
    FIRING  // alarm is going off
};

// Top-level menu entries, in the order they scroll.
enum MenuState {
    MENU_NONE = 0,
    MENU_TIME,
    MENU_DATE,
    MENU_MODE,
    MENU_ALARM
};

#define MENU_FIRST MENU_TIME
#define MENU_LAST MENU_ALARM

enum Action {
    ACTION_NONE,
    MENU_ENTER,   // clock -> menu
    MENU_EXIT,    // back out one level, discarding uncommitted edits
    MENU_UP,      // next entry / next field value
    MENU_DOWN,    // previous entry / previous field value
    MENU_SELECT,  // enter the highlighted entry
    EDIT_NEXT,    // advance to the next field, committing after the last
    MENU_TIMEOUT, // inactivity: straight back to the clock, discarding edits
    ALARM_FIRE,
    ALARM_DISMISS
};

// How many editable fields each entry has.
uint8_t menuFieldCount(MenuState item);

class ShiftDisplayFSM {
private:
    State currentState;
    State nextState;

    MenuState currentMenuState;
    MenuState nextMenuState;

    uint8_t currentField;
    uint8_t nextField;

    // Set for one update() when an editor's last field was advanced past,
    // so the caller knows to write the edited values through.
    bool commitPending;

public:
    ShiftDisplayFSM()
        : currentState(IDLE), nextState(IDLE),
          currentMenuState(MENU_NONE), nextMenuState(MENU_NONE),
          currentField(0), nextField(0), commitPending(false) {}

    void update();
    void execute(Action action);
    void setState(State state);

    State getState() const { return currentState; }
    MenuState getMenuState() const { return currentMenuState; }
    uint8_t getField() const { return currentField; }

    // True exactly once, on the update() that completes an editor.
    bool takeCommit();
};

#endif // _SHIFTDISPLAYFSM_H
