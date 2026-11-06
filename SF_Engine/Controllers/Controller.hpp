#pragma once

namespace SF::Engine
{
    /**
     * @brief Module functions without being a Module
     */
    class Controller
    {
    public:
        virtual ~Controller()
        {
            if (!shutDown)
                Controller::Shutdown();
        };

        virtual void Update(float /*DeltaTime*/) {}
        virtual void Initialize() {}
        virtual void Shutdown() { shutDown = true; }

        bool shutDown = false;
    };

    /**
     * @brief single instance version of @class Controller
     */
    template<typename T>
    class StaticController : public Controller
    {
    public:
        static T &Get() { return *instance; }

    protected:
        StaticController() { instance = static_cast<T *>(this); }

        ~StaticController() override
        {
            if (!shutDown)
                Controller::Shutdown();
            if (instance == this)
                instance = nullptr;
        }

    private:
        inline static T *instance = nullptr;
    };
} // namespace SF::Engine
