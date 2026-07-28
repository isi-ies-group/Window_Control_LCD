#ifndef STARTING_SCREENVIEW_HPP
#define STARTING_SCREENVIEW_HPP

#include <gui_generated/starting_screen_screen/STARTING_SCREENViewBase.hpp>
#include <gui/starting_screen_screen/STARTING_SCREENPresenter.hpp>

class STARTING_SCREENView : public STARTING_SCREENViewBase
{
public:
    STARTING_SCREENView();
    virtual ~STARTING_SCREENView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
protected:
};

#endif // STARTING_SCREENVIEW_HPP
