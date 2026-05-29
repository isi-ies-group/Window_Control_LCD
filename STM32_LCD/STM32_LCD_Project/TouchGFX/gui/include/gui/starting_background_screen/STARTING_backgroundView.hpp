#ifndef STARTING_BACKGROUNDVIEW_HPP
#define STARTING_BACKGROUNDVIEW_HPP

#include <gui_generated/starting_background_screen/STARTING_backgroundViewBase.hpp>
#include <gui/starting_background_screen/STARTING_backgroundPresenter.hpp>

class STARTING_backgroundView : public STARTING_backgroundViewBase
{
public:
    STARTING_backgroundView();
    virtual ~STARTING_backgroundView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
protected:
};

#endif // STARTING_BACKGROUNDVIEW_HPP
