/*
 * ==========================================================
 * ICommandHandler Data Categories and Method Mapping
 * ==========================================================
 *
 * This comment outlines how different types of system data should map to the 
 * ICommandHandler interface methods for consistency.
 *
 * Categories:
 *
 * 1. Sensor Input (Read-Only):
 *    - Description: Direct physical measurements from the simulation or hardware.
 *    - Examples: DEPTH, EXTERNAL_TEMPERATURE, TANK_LEVEL, DAMAGE_STATUS.
 *    - ICommandHandler Mapping:
 *      - QueryState: Handles requests for these aspects.
 *      - GetAvailableQueries: Lists these aspects.
 *      - HandleCommand: Does NOT handle commands to set these.
 *      - QueryEntireState: Does NOT include commands for these.
 *
 * 2. Computed Value (Read-Only):
 *    - Description: Values derived from sensor inputs or other state variables.
 *    - Examples: SPEED_OF_SOUND (from temp/salinity), POWER_DRAW (sum).
 *    - ICommandHandler Mapping:
 *      - QueryState: Handles requests for these aspects.
 *      - GetAvailableQueries: Lists these aspects.
 *      - HandleCommand: Does NOT handle commands to set these.
 *      - QueryEntireState: Does NOT include commands for these.
 *
 * 3. Internal State/Status (Read-Only Query):
 *    - Description: The current phase/status of an internal process or state machine.
 *    - Examples: AIRLOCK_STATE (FLOODING), TORPEDO_TUBE_STATE (LOADING), PID_IS_ENABLED (query-only).
 *    - ICommandHandler Mapping:
 *      - QueryState: Handles requests for these aspects.
 *      - GetAvailableQueries: Lists these aspects.
 *      - HandleCommand: Does NOT directly SET these (actions might change them).
 *      - QueryEntireState: Does NOT include commands for these.
 * 
 * 4. Actuator Feedback (Read-Only):
 *    - Description: The *actual* current measured state of an actuator (position, speed).
 *    - Examples: RUDDER_CURRENT_ANGLE, MOTOR_CURRENT_RPM.
 *    - ICommandHandler Mapping:
 *      - QueryState: Handles requests for these aspects.
 *      - GetAvailableQueries: Lists these aspects.
 *      - HandleCommand: Does NOT handle commands to set these.
 *      - QueryEntireState: Does NOT include commands for these.
 *
 * 5. Target State / Configuration (Read/Write):
 *    - Description: Desired state, configuration, or setpoint directly controllable via commands. This is the "memory" or restorable state.
 *    - Examples: HEADING_TARGET, POWER_ENABLED, JUNCTION_SELECTED_PORT, ACTUATOR_TARGET_ANGLE.
 *    - ICommandHandler Mapping:
 *      - HandleCommand: Processes verbs (SET, ENABLE) to modify this state.
 *      - QueryState: Retrieves the current value.
 *      - GetAvailableCommands: Lists the commands (e.g., "HEADING_TARGET SET <float>").
 *      - GetAvailableQueries: Lists the aspect (e.g., "HEADING_TARGET").
 *      - QueryEntireState: **Outputs commands to restore THIS state** (e.g., "HEADING_TARGET SET 180").
 *
 * 6. Action Triggers (Write-Only Command):
 *    - Description: Commands initiating a process or action, not setting a persistent state variable.
 *    - Examples: AIRLOCK_CYCLE START EXIT, TORPEDO_FIRE, SELF_DESTRUCT ENABLE.
 *    - ICommandHandler Mapping:
 *      - HandleCommand: Executes the action.
 *      - GetAvailableCommands: Lists the action command.
 *      - QueryState: Does NOT typically handle these.
 *      - GetAvailableQueries: Does NOT list these.
 *      - QueryEntireState: **NEVER includes commands for these.**
 *
 * Key Takeaways:
 * - GetAvailableQueries lists aspects for categories 1, 2, 3, 4, 5.
 * - GetAvailableCommands lists commands for categories 5, 6.
 * - QueryEntireState *only* outputs commands corresponding to category 5.
 */

#pragma once

#include "CoreMinimal.h"

class ICH_PowerJunction;

/**
 * Command result enumeration for HandleCommand.
 */
enum class ECommandResult
{
    Handled,
    Blocked,
    NotHandled,
    HandledWithError
};

/**
 * ICommandHandler Interface for handling submarine commands.
 */
class ICommandHandler
{
protected:
    /**
     * Unique identifier for the system handling commands.
     */
    FString SystemName;

    /**
     * Notification queue for messages and errors.
     */
    TArray<FString> NotificationQueue;

public:
    virtual ~ICommandHandler() = default;

    /**
     * Retrieves the system name.
     * @return The name of the system.
     */
    FString GetSystemName() const { return SystemName; }

    /**
     * Parses and executes a command.
     * @param Aspect The aspect of the system to target.
     * @param Command The command to execute.
     * @param Value The associated value for the command.
     * @return Command result indicating the execution status.
     */
    virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) = 0;
    virtual void PostHandleCommand() { };		// override this to rescan visual elements after a command

    /**
     * Checks if the handler can process a given command.
     * @param Aspect The aspect of the system to check.
     * @param Command The command to check.
     * @return True if the command is supported.
     */
    virtual bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const = 0;

    /**
     * Retrieves the value of a specific aspect.
     * @param Aspect The aspect of the system to query.
     * @return The string representation of the requested aspect.
     */
    virtual FString QueryState(const FString& Aspect) const = 0;

    /**
     * Retrieves the full state as a list of key-value command strings.
     * @return A list of all state values as command strings.
     */
    virtual TArray<FString> QueryEntireState() const = 0;

    /**
     * Returns a list of supported commands, without the system name.
     * @return A list of command strings without system name included.
     */
    virtual TArray<FString> GetAvailableCommands() const = 0;

    /**
     * Returns a list of supported queries
     * @return A list of aspects that can be queried.
     */
    virtual TArray<FString> GetAvailableQueries() const = 0;

    /**
     * Adds a message or error to the notification queue.
     * @param Message The message or error to be added.
     */
    void AddToNotificationQueue(const FString& Message)
    {
        NotificationQueue.Add(Message);
    }

    /**
     * Retrieves and clears the current queue of messages.
     * @return A list of notification messages.
     */
    TArray<FString> RetrieveNotificationQueue()
    {
        TArray<FString> Messages = NotificationQueue;
        NotificationQueue.Empty();
        return Messages;
    }

    /**
     * Gets the 2D position of the handler for visualization purposes.
     * @return The 2D position of the handler.
     */
    virtual FVector2D GetPosition() const { return FVector2D::ZeroVector; }

    /**
     * Performs per-frame updates.
     */
    virtual void Tick(float DeltaTime) = 0;

    /**
     * Allows safe casting to ICH_PowerJunction without RTTI.
     * Override in ICH_PowerJunction to return 'this'.
     * @return Pointer to this object if it's a ICH_PowerJunction, nullptr otherwise.
	 * used in void AVisualTestHarnessActor::InitializeVisualization and ICommandHandler
     */
    virtual class ICH_PowerJunction* GetAsPowerJunction() { return nullptr; }
    virtual const class ICH_PowerJunction* GetAsPowerJunction() const { return nullptr; }
};
