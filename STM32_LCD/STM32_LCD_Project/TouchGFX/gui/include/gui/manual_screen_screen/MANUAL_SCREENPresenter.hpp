#ifndef MANUAL_SCREENPRESENTER_HPP
#define MANUAL_SCREENPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class MANUAL_SCREENView;

class MANUAL_SCREENPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    MANUAL_SCREENPresenter(MANUAL_SCREENView& v);

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

    virtual ~MANUAL_SCREENPresenter() {}

private:
    MANUAL_SCREENPresenter();

    MANUAL_SCREENView& view;
};

#endif // MANUAL_SCREENPRESENTER_HPP
