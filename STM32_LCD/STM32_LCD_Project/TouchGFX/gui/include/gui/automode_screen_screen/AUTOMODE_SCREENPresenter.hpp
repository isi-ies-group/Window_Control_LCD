#ifndef AUTOMODE_SCREENPRESENTER_HPP
#define AUTOMODE_SCREENPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class AUTOMODE_SCREENView;

class AUTOMODE_SCREENPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    AUTOMODE_SCREENPresenter(AUTOMODE_SCREENView& v);

    /**
     * The activate function is called automatically when this screen is "switched in"
     * (ie. made active). Initialization logic can be placed here.
     */
    virtual void activate();

    /**
     * The deactivate function is called automatically when this screen is "switched out"
     * (ie. made inactive). Teardown functionality can be placed here.
     */
    virtual void deactivate();

    virtual ~AUTOMODE_SCREENPresenter() {}

private:
    AUTOMODE_SCREENPresenter();

    AUTOMODE_SCREENView& view;
};

#endif // AUTOMODE_SCREENPRESENTER_HPP
