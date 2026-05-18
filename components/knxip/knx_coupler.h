#pragma once
// KNX IP/TP Coupler — sdílený globální pointer na TP driver
// pro přístup z interval lambda
#include "knxtp.h"
#include "knxip_component.h"

// Globální pointer — nastaven v on_boot lambdě
esphome::knxip::KNXTPDriver* g_tp_driver = nullptr;

namespace esphome {
namespace knxip {

// IP → TP forwarder: zaregistruje se jako wildcard listener v KNXIPComponent
// Volá se pro KAŽDÝ přijatý IP telegram (nejen registrované GA)
// POZNAMKA: KNXIPComponent standardně dispatachuje jen registrované GA.
// Pro full bridge potřebujeme přidat add_forwarder() metodu do KNXIPComponent.
// Viz knxip_component.h níže.

}  // namespace knxip
}  // namespace esphome
