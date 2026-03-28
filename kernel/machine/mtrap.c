#include "kernel/process.h"
#include "kernel/riscv.h"
#include "spike_interface/spike_file.h"
#include "spike_interface/spike_utils.h"
#include "util/string.h"

//
// look up the faulting instruction address in the debug line table,
// then read and print the corresponding source file name and line.
//
static void print_error_line(uint64 epc) {
  // current is the global pointer to the running process (defined in
  // kernel/process.c)
  extern process *current;
  if (!current || !current->line || current->line_ind == 0)
    return;

  // Search the line table for the entry matching the faulting address.
  // The line table maps instruction addresses to source line numbers.
  int idx = -1;
  for (int i = 0; i < current->line_ind; i++) {
    if (current->line[i].addr == epc) {
      idx = i;
      break;
    }
  }
  if (idx < 0)
    return;

  // Retrieve the file and line number
  uint64 line_num = current->line[idx].line;
  uint64 file_idx = current->line[idx].file;
  char *filename = current->file[file_idx].file;
  uint64 dir_idx = current->file[file_idx].dir;
  char *dirname = current->dir[dir_idx];

  // Compose the full file path: "dir/file"
  char filepath[256];
  int k = 0;
  for (int i = 0; dirname[i] && k < 254; i++)
    filepath[k++] = dirname[i];
  if (k > 0 && filepath[k - 1] != '/')
    filepath[k++] = '/';
  for (int i = 0; filename[i] && k < 254; i++)
    filepath[k++] = filename[i];
  filepath[k] = '\0';

  sprint("Runtime error at %s:%d\n", filepath, line_num);

  // Open the source file via spike HTIF to read the error line
  spike_file_t *sf = spike_file_open(filepath, O_RDONLY, 0);
  if (IS_ERR_VALUE(sf))
    return;

  // Read the file content to find the target line.
  // We read in chunks and scan for newlines.
  char buf[256];
  uint64 current_line = 1;
  uint64 offset = 0;
  int found = 0;

  while (!found) {
    ssize_t nread = spike_file_pread(sf, buf, sizeof(buf) - 1, offset);
    if (nread <= 0)
      break;
    buf[nread] = '\0';

    for (int i = 0; i < nread; i++) {
      if (current_line == line_num) {
        // Found the target line - print from here to end of line
        // First, find where this line ends
        char line_buf[256];
        int j = 0;
        // Copy chars from current position to end of line (or buffer)
        while (i < nread && buf[i] != '\n' && j < 254) {
          line_buf[j++] = buf[i++];
        }
        // If we haven't reached end of line but ran out of buffer,
        // read more from file
        if (i >= nread && (j == 0 || line_buf[j - 1] != '\n')) {
          offset += nread;
          ssize_t more = spike_file_pread(sf, buf, sizeof(buf) - 1, offset);
          if (more > 0) {
            buf[more] = '\0';
            for (int m = 0; m < more && buf[m] != '\n' && j < 254; m++) {
              line_buf[j++] = buf[m];
            }
          }
        }
        line_buf[j] = '\0';
        // Trim leading whitespace for display, but show with indentation
        sprint("  %s\n", line_buf);
        found = 1;
        break;
      }
      if (buf[i] == '\n') {
        current_line++;
      }
    }
    offset += nread;
  }

  spike_file_close(sf);
}

static void handle_instruction_access_fault() {
  print_error_line(read_csr(mepc));
  panic("Instruction access fault!");
}

static void handle_load_access_fault() {
  print_error_line(read_csr(mepc));
  panic("Load access fault!");
}

static void handle_store_access_fault() {
  print_error_line(read_csr(mepc));
  panic("Store/AMO access fault!");
}

static void handle_illegal_instruction() {
  print_error_line(read_csr(mepc));
  panic("Illegal instruction!");
}

static void handle_misaligned_load() {
  print_error_line(read_csr(mepc));
  panic("Misaligned Load!");
}

static void handle_misaligned_store() {
  print_error_line(read_csr(mepc));
  panic("Misaligned AMO!");
}

// added @lab1_3
static void handle_timer() {
  int cpuid = 0;
  // setup the timer fired at next time (TIMER_INTERVAL from now)
  *(uint64 *)CLINT_MTIMECMP(cpuid) =
      *(uint64 *)CLINT_MTIMECMP(cpuid) + TIMER_INTERVAL;

  // setup a soft interrupt in sip (S-mode Interrupt Pending) to be handled in
  // S-mode
  write_csr(sip, SIP_SSIP);
}

//
// handle_mtrap calls a handling function according to the type of a machine
// mode interrupt (trap).
//
void handle_mtrap() {
  uint64 mcause = read_csr(mcause);
  switch (mcause) {
  case CAUSE_MTIMER:
    handle_timer();
    break;
  case CAUSE_FETCH_ACCESS:
    handle_instruction_access_fault();
    break;
  case CAUSE_LOAD_ACCESS:
    handle_load_access_fault();
  case CAUSE_STORE_ACCESS:
    handle_store_access_fault();
    break;
  case CAUSE_ILLEGAL_INSTRUCTION:
    // TODO (lab1_2): call handle_illegal_instruction to implement illegal
    // instruction interception, and finish lab1_2.
    handle_illegal_instruction();
    break;
  case CAUSE_MISALIGNED_LOAD:
    handle_misaligned_load();
    break;
  case CAUSE_MISALIGNED_STORE:
    handle_misaligned_store();
    break;

  default:
    sprint("machine trap(): unexpected mscause %p\n", mcause);
    sprint("            mepc=%p mtval=%p\n", read_csr(mepc), read_csr(mtval));
    panic("unexpected exception happened in M-mode.\n");
    break;
  }
}
