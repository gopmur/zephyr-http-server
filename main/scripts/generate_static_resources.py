import errno
import os
import random
import string
import sys


def generate_random_string(len=16):
  random_string = ""
  for _ in range(len):
    random_string += random.choice(string.ascii_letters + string.digits)
  return random_string


def generate_resource_includes(rel_path: str, base_path: str, resource_data_var_names: dict[str, str]) -> str:
  full_path = f"{base_path}/{rel_path}"
  entries = os.scandir(full_path)
  output = ""
  for entry in entries:
    if entry.is_dir():
      new_rel_path = f"{rel_path}/{entry.name}" if rel_path else entry.name
      output += generate_resource_includes(new_rel_path, base_path, resource_data_var_names)
    elif entry.is_file():
      resource_path = f"{rel_path}/{entry.name}" if rel_path else entry.name
      c_friendly_resource_path = resource_path.replace(
          ".", "_").replace("/", "_").replace("-", "_")
      resource_data_var_name = f"{c_friendly_resource_path}_data_{generate_random_string()}"
      resource_data_var_names[resource_path] = resource_data_var_name
      output += f"""
static const uint8_t {resource_data_var_name}[] = {{ 
  #include "assets/static/{resource_path}.gz.inc" 
}}; 
"""
  return output


def generate_resource_register_macro(rel_path: str, base_path: str, resource_data_var_names: dict[str, str]) -> str:
  full_path = f"{base_path}/{rel_path}"
  entries = os.scandir(full_path)
  output = ""
  for entry in entries:
    if entry.is_dir():
      new_rel_path = f"{rel_path}/{entry.name}" if rel_path else entry.name
      output += generate_resource_register_macro(new_rel_path, base_path, resource_data_var_names)
    elif entry.is_file():
      uri = f"/{rel_path}" if entry.name == "index.html" else f"/{rel_path}/{entry.name}" if rel_path else f"/{entry.name}"
      resource_path = f"{rel_path}/{entry.name}" if rel_path else entry.name
      c_friendly_resource_path = resource_path.replace(
          ".", "_").replace("/", "_").replace("-", "_")
      resource_detail_var_name = f"{c_friendly_resource_path}_detail_{generate_random_string()}"
      resource_data_var_name = resource_data_var_names[resource_path]
      resource_var_name = f"{c_friendly_resource_path}_{generate_random_string()}"

      output += f""" \\
static struct http_resource_detail_static {resource_detail_var_name} = {{ \\
  .common = \\
    {{ \\
      .bitmask_of_supported_http_methods = BIT(HTTP_GET), \\
      .content_encoding = "gzip", \\
      .content_type = "text/html", \\
      .type = HTTP_RESOURCE_TYPE_STATIC, \\
    }}, \\
  .static_data = {resource_data_var_name}, \\
  .static_data_len = sizeof({resource_data_var_name}), \\
}}; \\
HTTP_RESOURCE_DEFINE({resource_var_name}, http_service, "{uri}", &{resource_detail_var_name});"""
  return output


if __name__ == "__main__":
  if len(sys.argv) > 2:
    print("More than one arguments passed")
    exit(errno.E2BIG)
  if len(sys.argv) == 0:
    print("No path provided")
    exit(errno.EINVAL)
  path = sys.argv[1]
  try:
    os.listdir(path)
  except:
    print("Provided path was invalid")
    exit(errno.EINVAL)
  output = f"""
#pragma once
#include "zephyr/net/http/server.h"
#include "zephyr/net/http/service.h"

"""
  resource_data_var_names: dict[str, str] = {}
  output += generate_resource_includes("", path, resource_data_var_names)
  output += f"""#define REGISTER_STATIC_RESOURCES(http_server)"""
  output += generate_resource_register_macro("", path, resource_data_var_names)
  print(output)
