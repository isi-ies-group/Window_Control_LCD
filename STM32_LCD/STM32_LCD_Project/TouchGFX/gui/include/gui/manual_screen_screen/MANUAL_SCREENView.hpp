#ifndef MANUAL_SCREENVIEW_HPP
#define MANUAL_SCREENVIEW_HPP

#include <gui_generated/manual_screen_screen/MANUAL_SCREENViewBase.hpp>
#include <gui/manual_screen_screen/MANUAL_SCREENPresenter.hpp>

class MANUAL_SCREENView : public MANUAL_SCREENViewBase
{
public:
    MANUAL_SCREENView();
    virtual ~MANUAL_SCREENView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
protected:
};

#endif // MANUAL_SCREENVIEW_HPP
