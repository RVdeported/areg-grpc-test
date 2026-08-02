#pragma once

#include "areg-src/InterfaceConsumerBase.hpp"
#include "areg/component/Component.hpp"

#include "areg-src/Interface.hpp"

class WorkerComponent final : public areg::Component,
                              protected InterfaceConsumerBase
{
public:
  WorkerComponent(const areg::ComponentEntry & entry,
                  areg::ComponentThread & owner);

  // InterfaceConsumerBase overrides
protected:
  bool service_connected(areg::ServiceConnectionState status,
                         areg::ProxyBase & proxy) final;

  void response_AssignTaskReply(uint32_t task_id,
                                const Interface::DArray & array_a,
                                const Interface::DArray & array_b,
                                uint32_t rows_a, uint32_t cols_a,
                                uint32_t cols_b) final;
  
  void response_RegisterWorkerReply(uint32_t worker_id) final;

private:
  uint32_t mWorkerId{0};
  bool mRegistered{false};
  inline WorkerComponent & self() { return (*this); }

  WorkerComponent() = delete;
  AREG_NOCOPY_NOMOVE(WorkerComponent);
};
