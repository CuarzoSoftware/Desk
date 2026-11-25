#include <Nodes/DragGather.h>
#include <Nodes/DragStack.h>
#include <Core/Desk.h>
#include <CZ/Core/CZEase.h>
#include <cmath>

DragGather::Flyer::Flyer(MSurface &parent) noexcept : MSubsurface(&parent)
{
    setColor(CZAdaptiveColor(0x00000000));

    // Purely visual
    SkRegion empty;
    setCustomInputRegion(&empty);
    frame.setInputRegion(&empty);
    frame.setSizeMode(AKImageFrame::SizeMode::Fill);
    frame.layout().setWidthPercent(100.f);
    frame.layout().setHeightPercent(100.f);
}

DragGather::DragGather(MDND &dnd, const SkPoint &front, Int32 iconSize, const ABShadow &shadow,
                       std::vector<Flight> flights, std::function<void()> onFinished) noexcept :
    m_front(front),
    m_iconSize(iconSize),
    m_margin(Assets::ShadowMargin(shadow)),
    m_flights(std::move(flights)),
    m_onFinished(std::move(onFinished)),
    m_anim(DESK_DRAG_GATHER_MS,
        [this](CZAnimation *a){ step(a->value()); },
        [this](CZAnimation *){
            step(1.0);

            // Landed: hidden in the same commit of the drag icon where it becomes the stack
            for (auto &flyer : m_flyers)
                flyer->setMapped(false);

            if (m_onFinished)
                m_onFinished();
        })
{
    // The icons with their shadow, rendered once at the size they land with (scaled meanwhile)
    const Int32 scale { std::max(1, dnd.scale()) };
    const Int32 pixels { Int32(std::ceil(iconSize * (1.f + 2.f * m_margin))) * scale };
    auto &assets { GetDesk()->assets };

    // Stacked like the stack: created back to front, each one right below the drag icon (so above
    // the previous ones, and under the front icon)
    for (const auto &flight : m_flights)
    {
        auto &flyer { m_flyers.emplace_back(std::make_unique<Flyer>(dnd)) };
        flyer->placeBelow(nullptr);
        flyer->frame.setImage(assets.shadowedIcon(flight.image, pixels, scale, shadow));
    }

    step(0.0);

    for (auto &flyer : m_flyers)
        flyer->setMapped(true);

    m_anim.start();
}

DragGather::~DragGather() noexcept
{
    m_anim.pause();
}

void DragGather::step(Float64 progress) noexcept
{
    // Each one towards its layer of the stack (right and up by a step per layer from the front),
    // growing (or shrinking) to the stack's icon size
    const Int32 layerStep { DragStack::LayerStep(m_iconSize) };
    const SkScalar t { SkScalar(CZEase::OutCubic(progress)) };

    for (size_t i = 0; i < m_flyers.size(); i++)
    {
        const Flight &flight { m_flights[i] };
        const SkPoint to { m_front.x() + flight.depth * layerStep, m_front.y() - flight.depth * layerStep };
        const SkScalar size { flight.from.width() + (m_iconSize - flight.from.width()) * t };
        const SkScalar x { flight.from.x() + (to.x() - flight.from.x()) * t };
        const SkScalar y { flight.from.y() + (to.y() - flight.from.y()) * t };

        // The surface covers the shadow around the icon too
        const SkScalar margin { size * m_margin };
        const Int32 side { std::max(1, Int32(std::lround(size + 2.f * margin))) };
        auto &flyer { *m_flyers[i] };
        flyer.layout().setWidth(side);
        flyer.layout().setHeight(side);
        flyer.setPos(Int32(std::lround(x - margin)), Int32(std::lround(y - margin)));
    }
}
