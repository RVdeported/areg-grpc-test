#include "areg/appbase/Application.hpp"
#include "areg/component/ComponentLoader.hpp"

#include "MasterComponent.hpp"

#include "areg-src/Interface.hpp"

#include <common/utils.hpp>

#include <cstdlib>
#include <filesystem>
#include <print>
#include <string_view>
#include <vector>

// -- static task pool, populated by main() before model load ----------------
std::vector<common::Task> gMasterTasks;

constexpr char const _modelName[]{"MasterModel"};

BEGIN_MODEL(_modelName)
  BEGIN_REGISTER_THREAD("MasterThread")
    BEGIN_REGISTER_COMPONENT("MasterComponent", MasterComponent)
      REGISTER_IMPLEMENT_SERVICE(Interface::ServiceName, Interface::InterfaceVersion)
    END_REGISTER_COMPONENT("MasterComponent")
  END_REGISTER_THREAD("MasterThread")
END_MODEL(_modelName)

// =======================================================================
// main
// =======================================================================

static void usage(std::string_view prog)
{
  std::println(stderr, "usage: {} <task_file>", prog);
  std::exit(1);
}

int main(int argc, char ** argv)
{
  if (argc < 2)
    usage(argv[0]);

  // Capture CWD before the framework potentially changes it.
  common::g_output_dir = std::filesystem::current_path();

  // Pre-load tasks into static pool before model init.
  gMasterTasks = common::read_tasks(argv[1]);

  areg::Application::setup();
  areg::Application::load_model(_modelName);
  areg::Application::wait_quit(areg::WAIT_INFINITE);
  areg::Application::unload_model(_modelName);
  areg::Application::release();

  return 0;
}
