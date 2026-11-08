#pragma once
#include <Controllers/Controller.hpp>
#include <Math/KVP.hpp>
#include "Atmosphere/AtmosphereSFERenderpass.hpp"
#include "Clouds/CloudSFERenderpass.hpp"

#include <functional>
#include <string>
#include <utility>

using namespace std;
namespace SF::Engine
{
    struct AtmosphereEntry
    {
        std::string name                    = "Earth";
        shared_ptr<AtmosphereData> data     = nullptr;
        AtmosphereSFERenderpass *atmosphere = nullptr;
        CloudSFERenderpass *clouds          = nullptr;
        Vec3 planetPos                      = {0.0f, 0.0f, 0.0f};
        bool active                         = true;
    };

    class AtmosphereController : public StaticController<AtmosphereController>
    {
    public:
        using AtmoFactory  = std::function<AtmosphereSFERenderpass *(Pipeline::Stage, const AtmosphereParams &)>;
        using CloudFactory = function<CloudSFERenderpass *(Pipeline::Stage, shared_ptr<AtmosphereData> &)>;

        explicit AtmosphereController(Pipeline::Stage atmoStage, Pipeline::Stage cloudStage, AtmoFactory atmo,
                                      CloudFactory cloud) :
            atmoStage_(std::move(atmoStage)), cloudStage_(std::move(cloudStage)), atmoFactory_(std::move(atmo)),
            cloudFactory_(std::move(cloud))
        {
        }

        explicit AtmosphereController(Pipeline::Stage atmoStage, Pipeline::Stage cloudStage, AtmoFactory factory,
                                      CloudFactory cloud,
                                      const vector<KeyValuePair<std::string, shared_ptr<AtmosphereData>>> &params) :
            atmoStage_(std::move(atmoStage)), cloudStage_(std::move(cloudStage)), atmoFactory_(std::move(factory)),
            cloudFactory_(std::move(cloud))
        {
            entries_.reserve(params.size());
            for (const auto &[name, d]: params)
            {
                auto data = std::make_shared<AtmosphereData>(*d);
                entries_.emplace_back(AtmosphereEntry{name,
                                                      data,
                                                      atmoFactory_(atmoStage_, data->params),
                                                      cloudFactory_(cloudStage_, data),
                                                      {0.0f, 0.0f, 0.0f},
                                                      true});
            }
        }

        void Update(float /*DeltaTime*/) override
        {
            // this would be for clouds->SetFrameData(...);
        }

        void SetFrameData(const Mat4 &invProj, const Mat4 &invView, const Vec3 &cameraPos, const Vec3 &sunDir,
                          const Vec2 &screenSize) const
        {
            for (auto &entry: entries_)
            {
                if (!entry.active || !entry.atmosphere)
                    continue;

                entry.atmosphere->SetFrameData(invProj, invView, cameraPos, entry.planetPos, sunDir, screenSize);

                if (entry.clouds != nullptr)
                    entry.clouds->SetFrameData(invProj, invView, cameraPos, entry.planetPos, sunDir, screenSize);
            }
        }

        void SetActive(const std::string &name, const bool active)
        {
            if (AtmosphereEntry *entry = Find(name))
                entry->active = active;
        }

        void SetPlanetPosition(const std::string &name, const Vec3 &pos)
        {
            if (AtmosphereEntry *entry = Find(name))
                entry->planetPos = pos;
        }

        void AddAtmosphere(const std::string &name, const AtmosphereData &data, const Vec3 &planetPos = {})
        {
            auto sharedData = std::make_shared<AtmosphereData>(data);
            entries_.emplace_back(AtmosphereEntry{.name       = name,
                                                  .data       = sharedData,
                                                  .atmosphere = atmoFactory_(atmoStage_, sharedData->params),
                                                  .clouds     = cloudFactory_(cloudStage_, sharedData),
                                                  .planetPos  = planetPos,
                                                  .active     = true});
        }

        void RemoveAtmosphere(const std::string &name)
        {
            const auto newEnd =
                    ranges::remove_if(entries_, [&name](const AtmosphereEntry &e) { return e.name == name; }).begin();
            entries_.erase(newEnd, entries_.end());
        }

        [[nodiscard]] bool Empty() const { return entries_.empty(); }

        AtmosphereEntry *Find(const std::string &name)
        {
            for (auto &entry: entries_)
                if (entry.name == name)
                    return &entry;
            return nullptr;
        }

    private:
        Pipeline::Stage atmoStage_;
        Pipeline::Stage cloudStage_;
        AtmoFactory atmoFactory_;
        CloudFactory cloudFactory_;
        vector<AtmosphereEntry> entries_;
    };
} // namespace SF::Engine
