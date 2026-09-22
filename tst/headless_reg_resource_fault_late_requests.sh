#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS="resource=wl_pointer resource=wl_keyboard resource=wl_touch resource=wp_security_context_v1 resource=ext_image_capture_source_v1 resource=ext_image_copy_capture_session_v1 resource=ext_image_copy_capture_frame_v1 resource=ext_image_copy_capture_cursor_session_v1 resource=zwlr_screencopy_frame_v1 resource=wp_content_type_v1 resource=wp_alpha_modifier_surface_v1 resource=zxdg_output_v1 resource=wp_fractional_scale_v1 resource=zwp_relative_pointer_v1 resource=zwp_pointer_gesture_swipe_v1 resource=zwp_pointer_gesture_pinch_v1 resource=zwp_pointer_gesture_hold_v1 resource=zwp_locked_pointer_v1 resource=zwp_confined_pointer_v1 resource=zwp_keyboard_shortcuts_inhibitor_v1 resource=zwp_idle_inhibitor_v1 resource=ext_idle_notification_v1 resource=wl_buffer resource=xdg_toplevel_icon_v1 resource=wp_presentation_feedback resource=xdg_activation_token_v1 resource=zwp_linux_buffer_params_v1 resource=zwp_linux_dmabuf_feedback_v1 resource=zwp_linux_dmabuf_feedback_v1 resource=wp_cursor_shape_device_v1 resource=wp_color_management_output_v1 resource=wp_color_management_surface_v1 resource=wp_color_management_surface_feedback_v1 resource=wp_image_description_creator_params_v1 resource=wp_image_description_creator_icc_v1 resource=wp_image_description_v1 resource=wp_image_description_info_v1 resource=wp_color_representation_surface_v1"
# Each object the requests of the second half of the protocol table make
# fails to allocate once: the client asking for it gets no_memory.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
resource_fault_late_runs="
seat-pointer
seat-keyboard
seat-touch
security-context
capture-source
capture-session
capture-frame
capture-cursor
screencopy-frame
content-type
alpha-modifier
xdg-output
fractional-scale
relative-pointer
swipe
pinch
hold
locked-pointer
confined-pointer
shortcuts-inhibitor
idle-inhibitor
idle-notification
single-pixel-buffer
toplevel-icon
presentation-feedback
activation-token
dmabuf-params
dmabuf-default-feedback
dmabuf-surface-feedback
cursor-shape-device
colour-output
colour-surface
colour-feedback
colour-params
colour-icc
colour-description
colour-info
representation-surface
"
. "$(dirname "$0")/resource_fault_late_case.sh"
