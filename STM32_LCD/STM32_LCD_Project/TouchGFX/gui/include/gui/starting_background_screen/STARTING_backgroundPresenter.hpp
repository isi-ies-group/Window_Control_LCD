#ifndef STARTING_BACKGROUNDPRESENTER_HPP
#define STARTING_BACKGROUNDPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class STARTING_backgroundView;

class STARTING_backgroundPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    STARTING_backgroundPresenter(STARTING_backgroundView& v);

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

    virtual ~STARTING_backgroundPresenter() {}

private:
    STARTING_backgroundPresenter();

    STARTING_backgroundView& view;
};

#endif // STARTING_BACKGROUNDPRESENTER_HPP
