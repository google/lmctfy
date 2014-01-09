# lmctfy Codebase Primer

The goal of this document is to provide a quick-start guide on the design of lmctfy in order to make contributing to the codebase easier.

## Directory Structure

The repository has a slew of directories and a handful of files at the top level. Most of the top level files are documentation, licensing, and the build/test rules. The other directories are described below.

### lmctfy Core

The core of lmctfy can be found in `/lmctfy` and the public API can be found in `/include`.

* `/lmctfy`: The implementation of the public API and the core abstractions [(described below)](#abstractions).
* `/lmctfy/cli`: The implementation of the lmctfy command line tool.
* `/lmctfy/controllers`: [Controllers](#controllers) for all resources lmctfy supports.
* `/lmctfy/resources`: [Resource handlers](#resource-handlers) for all resources lmctfy supports.

### Utilities &amp; Dependencies

This comprises the majority of the top-level directories. These are base and generic C++ libraries and utilities.

* `/base`: Base libraries.
* `/file`: File-handling utilities.
* `/gmock`: The gMock and gTest frameworks.
* `/strings`: String-handling utilities.
* `/thread`: Wrapper for thread operations.
* `/util`: Generic utilities.

### System

The lmctfy code abstracts system interactions with the system API found in `/system_api`. This abstraction lets us mock out the system for unit and integration testing.

## Abstractions

At the core of lmctfy is the public C++ library API (the CLI is a thin wrapper around this). The public API has the `ContainerApi` which functions as a factory of `Container` objects (both of these are in [lmctfy.h](/include/lmctfy.h)). These make heavy use of different specifications found in [lmctfy.proto](/include/lmctfy.proto). All of these specifications are split into **resources** that lmctfy supports. The aim is for these to not change significantly over time and to be high level enough to adapt to underlying kernel changes. 

The C++ library is implemented by some logic wrapping a set of **handlers**. The most prevalent of these are the **resource handlers** which each implements the support and policies of a single resource (their interface is in [resource_handler.h](/lmctfy/resource_handler.h)). One other top-level handler is the **tasks handler** which manages tracking of tasks (interface at [tasks_handler.h](/lmctfy/tasks_handler.h)).

### Resource Handlers

Each resource handler defines its own specification and implements the isolation that specification mentions. It does this through a series of [controllers](#controllers) which interact with the kernel. The `ResouceHandler` itself decides how it will use the controllers to implement the desired isolation (i.e.: the `CpuResourceHandler` creates a `/batch` top-level `cpu` cgroup (through the `CpuController`) to isolate all batch workload).

#### Controllers

The controllers are what actually interacts with the kernel to implement the desired isolation. They provide a slightly higher API to the resource handlers in order to encapsulate changes in the kernel. The granularity of a controller is a cgroup or a namespace (e.g.: `CpuController` for the `cpu` hierarchy and `MemoryController` for the `memory` hierarchy).

### Tasks Handler

lmctfy requires a canonical tasks hierarchy it uses to keep track of groups of tasks. The `TasksHandler` contains this logic. The current implementation is the `CgroupTasksHandler` which uses the `freezer` cgroup hierarchy to track tasks.

## Other

Feel free to e-mail the project's mailing list with any questions: <lmctfy@googlegroups.com>.
