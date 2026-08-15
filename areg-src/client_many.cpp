#include "areg/appbase/Application.hpp"
#include "areg/component/ComponentLoader.hpp"

#include "WorkerComponent.hpp"
#include "common/utils.hpp"
#include <filesystem>

constexpr char const _modelName[]{"WorkerModel"};

#define REGISTER_WORKER(index) \
  BEGIN_REGISTER_THREAD("WorkerThread_" #index) \
    BEGIN_REGISTER_COMPONENT("WorkerComponent_" #index, WorkerComponent) \
      REGISTER_DEPENDENCY("MasterComponent") \
    END_REGISTER_COMPONENT("WorkerComponent_" #index) \
  END_REGISTER_THREAD("WorkerThread_" #index)

// Usage
BEGIN_MODEL(_modelName)
REGISTER_WORKER(1)
REGISTER_WORKER(2)
REGISTER_WORKER(3)
REGISTER_WORKER(4)
REGISTER_WORKER(5)
REGISTER_WORKER(6)
REGISTER_WORKER(7)
REGISTER_WORKER(8)
REGISTER_WORKER(9)
REGISTER_WORKER(10)
REGISTER_WORKER(11)
REGISTER_WORKER(12)
REGISTER_WORKER(13)
REGISTER_WORKER(14)
REGISTER_WORKER(15)
REGISTER_WORKER(16)
REGISTER_WORKER(17)
REGISTER_WORKER(18)
REGISTER_WORKER(19)
REGISTER_WORKER(20)
REGISTER_WORKER(21)
REGISTER_WORKER(22)
REGISTER_WORKER(23)
REGISTER_WORKER(24)
REGISTER_WORKER(25)
REGISTER_WORKER(26)
REGISTER_WORKER(27)
REGISTER_WORKER(28)
REGISTER_WORKER(29)
REGISTER_WORKER(30)
REGISTER_WORKER(31)
REGISTER_WORKER(32)
REGISTER_WORKER(33)
REGISTER_WORKER(34)
REGISTER_WORKER(35)
REGISTER_WORKER(36)
REGISTER_WORKER(37)
REGISTER_WORKER(38)
REGISTER_WORKER(39)
REGISTER_WORKER(40)
REGISTER_WORKER(41)
REGISTER_WORKER(42)
REGISTER_WORKER(43)
REGISTER_WORKER(44)
REGISTER_WORKER(45)
REGISTER_WORKER(46)
REGISTER_WORKER(47)
REGISTER_WORKER(48)
REGISTER_WORKER(49)
REGISTER_WORKER(50)
REGISTER_WORKER(51)
REGISTER_WORKER(52)
REGISTER_WORKER(53)
REGISTER_WORKER(54)
REGISTER_WORKER(55)
REGISTER_WORKER(56)
REGISTER_WORKER(57)
REGISTER_WORKER(58)
REGISTER_WORKER(59)
REGISTER_WORKER(60)
END_MODEL(_modelName)

  // =======================================================================
// main
// =======================================================================

int main()
{

  common::g_output_dir = std::filesystem::current_path();
  areg::Application::setup(true, true, true, true, true);
  areg::Application::load_model(_modelName);
  areg::Application::wait_quit(areg::WAIT_INFINITE);
  areg::Application::unload_model(_modelName);
  areg::Application::release();

  return 0;
}
