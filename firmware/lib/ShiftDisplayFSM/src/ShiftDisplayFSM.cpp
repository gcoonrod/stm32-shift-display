#include "ShiftDisplayFSM.h"

void ShiftDisplayFSM::update()
{
    currentState = nextState;
}

void ShiftDisplayFSM::setState(State state)
{
    nextState = state;
}

State ShiftDisplayFSM::getState()
{
    return currentState;
}

void ShiftDisplayFSM::execute(Action action)
{
    switch (action)
    {
    case Action::MENU_ENTER:
        if (currentState == State::IDLE)
        {
            nextState = State::MENU;
            nextMenuState = MenuState::MENU_SET_TIME;
        }
        break;
    case Action::MENU_EXIT:
        nextState = State::IDLE;
        nextMenuState = MenuState::MENU_NONE;
        break;
    case Action::MENU_UP:
        break;
    case Action::MENU_DOWN:
        break;
    case Action::MENU_SELECT:
        if (currentState == State::IDLE)
            break;
        switch (currentMenuState)
        {
        case MenuState::MENU_SET_TIME:
            nextMenuState = MenuState::MENU_SET_HOUR;
            break;
        case MenuState::MENU_SET_DATE:
            nextMenuState = MenuState::MENU_SET_DAY;
            
            break;
        default:
            break;
        }
    }
}