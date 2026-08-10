# Development Instructions

## Project

This repository contains the software for an automated
3-axis tensile testing rig.

The system consists of:

- Raspberry Pi 5
- Node.js GUI
- STM32F446RE controller
- 3-axis motion system
- force sensor
- pneumatic actuator

## Architecture

The software is divided into:

- Core
- Simulation
- STM32 Firmware
- GUI
- Communication

The Core must remain platform independent.

The Core must NOT directly depend on:

- STM32 HAL
- Arduino libraries
- Windows APIs
- Linux APIs
- Node.js APIs

## Development principles

- Keep the architecture modular.
- Prefer simple solutions over unnecessary abstractions.
- Avoid blocking control loops.
- Use state machines for machine processes.
- STOP must always be handled.
- Simulation and STM32 shall use the same Core logic where possible.
- Do not implement detailed physics unless explicitly required.
- Do not modify unrelated files.
- Do not introduce dependencies without justification.
- Build and test after significant changes.

## Agent behavior

Before implementing a major architectural change:

1. Read /docs/Project_Specification.md and /docs/Software_Architecture.md and /docs/Development_Rules.md before making architectural changes.
2. Explain the proposed change.
3. Identify affected modules.
4. Wait for approval if the change modifies the architecture.

For normal implementation tasks, implement only the requested scope.