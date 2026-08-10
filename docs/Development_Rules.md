# Development Rules

1. Do not implement features that were not requested.

2. Do not modify the architecture without explaining why.

3. Keep the Core platform independent.

4. Avoid dynamic memory allocation in the STM32 Core.

5. Prefer deterministic behavior.

6. Avoid blocking loops in machine control logic.

7. Machine control must use non-blocking state machines.

8. Every machine state must be interruptible by STOP.

9. PAUSE and STOP must be handled without blocking the main control loop.

10. Do not use STM32 HAL functions inside Core classes.

11. Simulation behavior should approximate machine behavior,
    not reproduce detailed physical dynamics.

12. Keep classes small and single-purpose.

13. Do not create unnecessary abstractions.

14. Compile and test after each significant change.

15. Do not modify unrelated files.

16. Comment every created code line.

17. Keep the architecture modular.