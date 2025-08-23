/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * This file is part of the TinyUSB stack.
 */

#include "tusb_option.h"

#if (CFG_TUH_ENABLED && CFG_TUH_VENDOR)

  //--------------------------------------------------------------------+
  // INCLUDE
  //--------------------------------------------------------------------+
  #include "host/usbh.h"
  #include "host/usbh_pvt.h"
  #include "vendor_host.h"

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF
//--------------------------------------------------------------------+

//--------------------------------------------------------------------+
// INTERNAL OBJECT & FUNCTION DECLARATION
//--------------------------------------------------------------------+
typedef struct {
  uint8_t itf_num;
  uint8_t ep_in;
  uint8_t ep_out;
  bool setup;
} custom_interface_info_t;

custom_interface_info_t custom_interface[CFG_TUH_DEVICE_MAX];


TU_ATTR_ALWAYS_INLINE static inline custom_interface_info_t *get_itf(uint8_t daddr) {
  return &custom_interface[daddr - 1];
}

TU_ATTR_ALWAYS_INLINE static inline bool tusbh_custom_is_mounted(uint8_t dev_addr, uint16_t vendor_id, uint16_t product_id) {
  uint16_t vid, pid;
  TU_ASSERT(tuh_vid_pid_get(dev_addr, &vid, &pid));
  
  custom_interface_info_t *p_cust = get_itf(dev_addr);
  return p_cust->setup && vid == vendor_id && pid == product_id;
}


static bool cush_validate_paras(uint8_t dev_addr, uint16_t vendor_id, uint16_t product_id, void *p_buffer, uint16_t length) {
  if (!tusbh_custom_is_mounted(dev_addr, vendor_id, product_id)) {
    TU_LOG1("Device not mounted or not setup!\n");
    return false;
  }

  TU_ASSERT(p_buffer != NULL && length != 0);

  return true;
}
//--------------------------------------------------------------------+
// APPLICATION API (need to check parameters)
//--------------------------------------------------------------------+
bool tusbh_custom_read(uint8_t dev_addr, uint16_t vendor_id, uint16_t product_id, uint8_t *p_buffer, uint16_t length) {
  TU_ASSERT(cush_validate_paras(dev_addr, vendor_id, product_id, p_buffer, length));

  custom_interface_info_t *p_cust = get_itf(dev_addr);

  if (!usbh_edpt_claim(dev_addr, p_cust->ep_in)) {
    TU_LOG1("Couldn't claim endpoint :c\n");
    return false;
  }

  if (!usbh_edpt_xfer(dev_addr, p_cust->ep_in, p_buffer, length)) {
    usbh_edpt_release(dev_addr, p_cust->ep_in);
    TU_LOG1("Couldn't read from endpoint :cc\n");
    return false;
  }
  return true;
}

bool tusbh_custom_write(uint8_t dev_addr, uint16_t vendor_id, uint16_t product_id, uint8_t *p_data, uint16_t length) {
  TU_ASSERT(cush_validate_paras(dev_addr, vendor_id, product_id, p_data, length));

  custom_interface_info_t *p_cust = get_itf(dev_addr);

  if (!usbh_edpt_claim(dev_addr, p_cust->ep_out)) {
    TU_LOG1("Couldn't claim endpoint :c\n");
    return false;
  }

  if (!usbh_edpt_xfer(dev_addr, p_cust->ep_out, p_data, length)) {
    usbh_edpt_release(dev_addr, p_cust->ep_out);
    TU_LOG1("Couldn't transfer to endpoint :cc\n");
    return false;
  }
  return true;
}

//--------------------------------------------------------------------+
// USBH-CLASS API
//--------------------------------------------------------------------+
bool cush_init(void) {
  tu_memclr(&custom_interface, sizeof(custom_interface_info_t) * CFG_TUH_DEVICE_MAX);
  return true;
}

bool cush_deinit(void) { return true; }

bool cush_open_subtask(uint8_t rhport, uint8_t dev_addr, tusb_desc_interface_t const *itf_desc, uint16_t max_len) {
  uint8_t const *p_desc = tu_desc_next(itf_desc);
  uint8_t const* p_desc_end = ((uint8_t const*) itf_desc) + max_len;

  custom_interface_info_t *p_cust = get_itf(dev_addr);
  p_cust->setup = false;

  //------------- Bulk Endpoints Descriptor -------------//

  // We expect two endpoints for the heat it
  TU_LOG3("Num endpoints: %d\n", itf_desc->bNumEndpoints);
  TU_ASSERT(itf_desc->bNumEndpoints == 2);

  for (uint8_t i = 0; i < 2; i++) {
    TU_ASSERT(p_desc < p_desc_end);
    TU_ASSERT(TUSB_DESC_ENDPOINT == tu_desc_type(p_desc));
    tusb_desc_endpoint_t const *p_endpoint = (tusb_desc_endpoint_t const *) p_desc;

    TU_LOG3("Open endpoint #%d\n", i);
    TU_ASSERT(tuh_edpt_open(dev_addr, p_endpoint));

    if (TUSB_DIR_IN == tu_edpt_dir(p_endpoint->bEndpointAddress)) {
      p_cust->ep_in = p_endpoint->bEndpointAddress;
    } else {
      p_cust->ep_out = p_endpoint->bEndpointAddress;
    }

    p_desc = tu_desc_next(p_desc);
  }

  p_cust->itf_num = itf_desc->bInterfaceNumber;
  p_cust->setup = true;

  TU_LOG3("Successfully setup!\n");

  return true;
}

bool cush_set_config(uint8_t dev_addr, uint8_t itf_num) {
  custom_interface_info_t *p_hub = get_itf(dev_addr);
  TU_LOG3("Requesting to set interface %d and got %d\n", itf_num, p_hub->itf_num);
  TU_ASSERT(itf_num == p_hub->itf_num);

  // // Get Hub Descriptor
  // tusb_control_request_t const request =
  // {
  //   .bmRequestType_bit =
  //   {
  //     .recipient = TUSB_REQ_RCPT_DEVICE,
  //     .type      = TUSB_REQ_TYPE_CLASS,
  //     .direction = TUSB_DIR_IN
  //   },
  //   .bRequest = HUB_REQUEST_GET_DESCRIPTOR,
  //   .wValue   = 0,
  //   .wIndex   = 0,
  //   .wLength  = sizeof(descriptor_hub_desc_t)
  // };

  // tuh_xfer_t xfer =
  // {
  //   .daddr       = dev_addr,
  //   .ep_addr     = 0,
  //   .setup       = &request,
  //   .buffer      = _hub_buffer,
  //   .complete_cb = config_set_port_power,
  //   .user_data    = 0
  // };

  // TU_ASSERT( tuh_control_xfer(&xfer) );

  return true;
}


TU_ATTR_WEAK bool cush_xfer_cb(uint8_t dev_addr, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes) {
  // TODO handle stall response, retry failed transfer ...
  TU_ASSERT(result == XFER_RESULT_SUCCESS);
  return true;
}

void cush_close(uint8_t dev_addr) {
  TU_VERIFY(dev_addr <= CFG_TUH_DEVICE_MAX, );
  custom_interface_info_t *p_cust = get_itf(dev_addr);

  tu_memclr(p_cust, sizeof(custom_interface_info_t));
}

#endif
