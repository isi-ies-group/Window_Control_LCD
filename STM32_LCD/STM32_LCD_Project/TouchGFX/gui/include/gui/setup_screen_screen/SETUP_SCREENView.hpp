#ifndef SETUP_SCREENVIEW_HPP
#define SETUP_SCREENVIEW_HPP

#include <gui_generated/setup_screen_screen/SETUP_SCREENViewBase.hpp>
#include <gui/setup_screen_screen/SETUP_SCREENPresenter.hpp>

class SETUP_SCREENView : public SETUP_SCREENViewBase
{
public:
    SETUP_SCREENView();
    virtual ~SETUP_SCREENView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
protected:
};

#endif // SETUP_SCREENVIEW_HPP
