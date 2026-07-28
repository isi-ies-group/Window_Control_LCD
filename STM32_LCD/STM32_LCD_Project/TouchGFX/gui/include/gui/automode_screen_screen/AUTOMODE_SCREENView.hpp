#ifndef AUTOMODE_SCREENVIEW_HPP
#define AUTOMODE_SCREENVIEW_HPP

#include <gui_generated/automode_screen_screen/AUTOMODE_SCREENViewBase.hpp>
#include <gui/automode_screen_screen/AUTOMODE_SCREENPresenter.hpp>

class AUTOMODE_SCREENView : public AUTOMODE_SCREENViewBase
{
public:
    AUTOMODE_SCREENView();
    virtual ~AUTOMODE_SCREENView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
protected:
};

#endif // AUTOMODE_SCREENVIEW_HPP
