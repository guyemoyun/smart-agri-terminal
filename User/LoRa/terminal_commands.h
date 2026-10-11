#ifndef TERMINAL_COMMANDS_H
#define TERMINAL_COMMANDS_H
#include "lora_protocol.h"
#include "control_service.h"
#define TERMINAL_COMMAND_CACHE 8U

typedef struct
{
    void *context;
    uint8_t (*execute)(void *context,uint8_t opcode,uint8_t argument,uint32_t now);
    void (*snapshot)(void *context,ControlSnapshot_t *snapshot);
} TerminalExecutor_t;
typedef struct
{
    uint8_t valid,opcode,argument;
    uint16_t sequence;
    uint32_t transaction,expires;
    uint8_t result[LORA_RESULT_SIZE];
} TerminalCommandRecord_t;
typedef struct
{
    uint16_t node,gateway;
    uint32_t session,high_water;
    uint8_t next_slot;
    TerminalCommandRecord_t records[TERMINAL_COMMAND_CACHE];
} TerminalCommands_t;
typedef enum
{
    TERMINAL_COMMAND_DROP=0,TERMINAL_COMMAND_NEW,TERMINAL_COMMAND_DUPLICATE,
    TERMINAL_COMMAND_STALE,TERMINAL_COMMAND_CONFLICT
} TerminalCommandOutcome_t;

void TerminalCommands_Init(TerminalCommands_t *engine,uint16_t node,uint16_t gateway,uint32_t session);
TerminalCommandOutcome_t TerminalCommands_Process(TerminalCommands_t *engine,const LoraFrame_t *command,
    const TerminalExecutor_t *executor,uint32_t now,LoraFrame_t *response);
const TerminalExecutor_t *TerminalCommands_ControlExecutor(void);
#endif
