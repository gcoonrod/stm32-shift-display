#ifndef _SHIFTDISPLAYFSM_H
#define _SHIFTDISPLAYFSM_H

#include <inttypes.h>

enum State {
    IDLE, MENU
};

enum MenuState {
    MENU_NONE, MENU_SET_TIME, MENU_SET_DATE, MENU_SET_HOUR, MENU_SET_MINUTE, MENU_SET_SECOND, MENU_SET_DAY, MENU_SET_MONTH, MENU_SET_YEAR
};

enum Action {
    ACTION_NONE, MENU_ENTER, MENU_EXIT, MENU_UP, MENU_DOWN, MENU_SELECT
};

class ShiftDisplayFSM {
private:
    State currentState;
    State nextState;

    MenuState currentMenuState;
    MenuState nextMenuState;

public:
    ShiftDisplayFSM() : currentState(IDLE), nextState(IDLE), currentMenuState(MENU_NONE), nextMenuState(MENU_NONE) {}
    void update();
    void execute(Action action);
    void setState(State state);
    State getState();
};



#endif // _SHIFTDISPLAYFSM_H
