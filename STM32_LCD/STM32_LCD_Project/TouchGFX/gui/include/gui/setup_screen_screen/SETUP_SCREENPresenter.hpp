#ifndef SETUP_SCREENPRESENTER_HPP
#define SETUP_SCREENPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class SETUP_SCREENView;

class SETUP_SCREENPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    SETUP_SCREENPresenter(SETUP_SCREENView& v);

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

    virtual ~SETUP_SCREENPresenter() {}

private:
    SETUP_SCREENPresenter();

    SETUP_SCREENView& view;
};

#endif // SETUP_SCREENPRESENTER_HPP
