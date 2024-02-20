#!/bin/bash

sudo mount -t tmpfs -o size=2g,mpol=bind:1 tmpfs /testcxl