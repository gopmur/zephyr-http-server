import errno
import os
import sys


def generate_static_resources(rel_path: str, base_path: str) -> str:
  full_path = f"{base_path}/{rel_path}"
  entries = os.scandir(full_path)
  output = ""
  for entry in entries:
    if entry.is_dir():
      new_rel_path = f"{rel_path}/{entry.name}" if rel_path else entry.name
      output += generate_static_resources(new_rel_path, base_path)
    elif entry.is_file():
      uri = f"/{rel_path}" if entry.name == "index.html" else f"/{rel_path}/{entry.name}" if rel_path else f"/{entry.name}"
      resource_path = f"{rel_path}/{entry.name}" if rel_path else entry.name
      c_friendly_resource_path = resource_path.replace(
          ".", "_").replace("/", "_").replace("-", "_")
      output += f"""
static const uint8_t {c_friendly_resource_path}[] = {{ 
  #include "assets/static/{resource_path}.gz.inc" 
}}; 

static struct http_resource_detail_static {c_friendly_resource_path}_detail = {{ 
  .common = 
    {{ 
      .bitmask_of_supported_http_methods = BIT(HTTP_GET), 
      .content_encoding = "gzip", 
      .content_type = "text/html", 
      .type = HTTP_RESOURCE_TYPE_STATIC, 
    }}, 
  .static_data = {c_friendly_resource_path}, 
  .static_data_len = sizeof({c_friendly_resource_path}), 
}}; 
HTTP_RESOURCE_DEFINE({c_friendly_resource_path}_resource, http_service, "{uri}", &{c_friendly_resource_path}_detail); 
"""
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

  output += generate_static_resources("", path)
  print(output)
