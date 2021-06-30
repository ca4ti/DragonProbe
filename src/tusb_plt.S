@ vim: set ft=armv5:

.cpu cortex-m0
.thumb

.section .data.tusb_gotplt, "awx", %progbits
.global tusb_plt
tusb_plt:

	@ sigh, thumb...
.type tud_hid_get_report_cb, %function
.global tud_hid_get_report_cb
tud_hid_get_report_cb:
	push {r0}
	ldr   r0, .Lhid_get_report
	mov  r12, r0
	pop  {r0}
	bx   r12

.type tud_hid_set_report_cb, %function
.global tud_hid_set_report_cb
tud_hid_set_report_cb:
	push {r0}
	ldr   r0, .Lhid_set_report
	mov  r12, r0
	pop  {r0}
	bx   r12

.type tud_cdc_line_coding_cb, %function
.global tud_cdc_line_coding_cb
tud_cdc_line_coding_cb:
	push {r0}
	ldr   r0, .Lcdc_line_coding
	mov  r12, r0
	pop  {r0}
	bx   r12

.type tud_vendor_control_xfer_cb, %function
.global tud_vendor_control_xfer_cb
tud_vendor_control_xfer_cb:
	push {r0}
	ldr   r0, .Lvendor_control_xfer
	mov  r12, r0
	pop  {r0}
	bx   r12



.type tud_hid_descriptor_report_cb, %function
.global tud_hid_descriptor_report_cb
tud_hid_descriptor_report_cb:
	push {r0}
	ldr   r0, .Lhid_descriptor_report
	mov  r12, r0
	pop  {r0}
	bx   r12

.type tud_descriptor_device_cb, %function
.global tud_descriptor_device_cb
tud_descriptor_device_cb:
	push {r0}
	ldr   r0, .Ldescriptor_device
	mov  r12, r0
	pop  {r0}
	bx   r12

.type tud_descriptor_configuration_cb, %function
.global tud_descriptor_configuration_cb
tud_descriptor_configuration_cb:
	push {r0}
	ldr   r0, .Ldescriptor_configuration
	mov  r12, r0
	pop  {r0}
	bx   r12

.type tud_descriptor_string_cb, %function
.global tud_descriptor_string_cb
tud_descriptor_string_cb:
	push {r0}
	ldr   r0, .Ldescriptor_string
	mov  r12, r0
	pop  {r0}
	bx   r12



.type tusb_got, %object
.size tusb_got, 4*8
.global tusb_got
tusb_got:
.Lhid_get_report:
	.4byte 0 @ tud_hid_get_report_cb
.Lhid_set_report:
	.4byte 0 @ tud_hid_set_report_cb
.Lcdc_line_coding:
	.4byte 0 @ tud_cdc_line_coding_cb
.Lvendor_control_xfer:
	.4byte 0 @ tud_vendor_control_xfer_cb

.Lhid_descriptor_report:
	.4byte 0 @ tud_hid_descriptor_report_cb
.Ldescriptor_device:
	.4byte 0 @ tud_descriptor_device_cb
.Ldescriptor_configuration:
	.4byte 0 @ tud_descriptor_configuration_cb
.Ldescriptor_string:
	.4byte 0 @ tud_descriptor_string_cb

