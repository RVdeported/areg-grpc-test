#include "areg/appbase/Application.hpp"
#include "areg/component/ComponentLoader.hpp"

#include "WorkerComponent.hpp"

constexpr char const _modelName[]{"WorkerModel"};

BEGIN_MODEL(_modelName)
  BEGIN_REGISTER_THREAD("WorkerThread")
    BEGIN_REGISTER_COMPONENT("WorkerComponent", WorkerComponent)
      REGISTER_DEPENDENCY("MasterComponent")
    END_REGISTER_COMPONENT("WorkerComponent")
  END_REGISTER_THREAD("WorkerThread")
END_MODEL(_modelName)

// =======================================================================
// main
// =======================================================================

int main()
{
  areg::Application::setup();
  areg::Application::load_model(_modelName);
  areg::Application::wait_quit(areg::WAIT_INFINITE);
  areg::Application::unload_model(_modelName);
  areg::Application::release();

  return 0;
}
