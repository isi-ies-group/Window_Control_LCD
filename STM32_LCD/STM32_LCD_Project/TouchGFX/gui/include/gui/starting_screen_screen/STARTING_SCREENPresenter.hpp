#ifndef STARTING_SCREENPRESENTER_HPP
#define STARTING_SCREENPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class STARTING_SCREENView;

class STARTING_SCREENPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    STARTING_SCREENPresenter(STARTING_SCREENView& v);

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

    virtual ~STARTING_SCREENPresenter() {}

private:
    STARTING_SCREENPresenter();

    STARTING_SCREENView& view;
};

#endif // STARTING_SCREENPRESENTER_HPP
