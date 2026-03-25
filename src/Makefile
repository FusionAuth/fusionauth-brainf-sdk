# ============================================================
# Makefile for FusionAuth Brainfuck SDK
#
# Build pipeline:
#   fusionauth_bf.c → (8cc) → fusionauth.eir → (elc) → fusionauth.bf
#
# Prerequisites:
#   - ELVM toolchain: git clone https://github.com/shinh/elvm && cd elvm && make
#   - Python 3 + requests: pip install requests
#   - A running FusionAuth instance (default: http://localhost:9011)
#
# Quick start:
#   make test-native    # Test with gcc (fast, for development)
#   make eir            # Compile to ELVM IR (medium)
#   make test-eir       # Test via ELVM IR interpreter
#   make bf             # Compile to Brainfuck (the big one)
#   make run            # Run the Brainfuck SDK for real
# ============================================================

# Path to your ELVM checkout
ELVM_DIR ?= ./elvm

# ELVM tools
ECC      = $(ELVM_DIR)/out/8cc
ELC      = $(ELVM_DIR)/out/elc
ELI      = $(ELVM_DIR)/out/eli
BFOPT    = $(ELVM_DIR)/out/bfopt

# Source
SRC      = fusionauth_bf.c
EIR      = fusionauth.eir
BF       = fusionauth.bf
NATIVE   = fusionauth_native

# FusionAuth config
FUSIONAUTH_URL ?= http://localhost:9011

# ---- Native build (for rapid testing) ----

$(NATIVE): $(SRC)
	gcc -Wall -Wextra -o $@ $<

native: $(NATIVE)

test-native: $(NATIVE)
	python3 runner.py --native ./$(NATIVE) --url $(FUSIONAUTH_URL) -v

# ---- ELVM IR compilation ----

$(EIR): $(SRC) $(ECC)
	$(ECC) -S -o $@ $<

eir: $(EIR)

test-eir: $(EIR)
	python3 runner.py --eir-interpreter $(ELI) --eir-program $(EIR) --url $(FUSIONAUTH_URL) -v

# ---- Brainfuck compilation (the main event) ----

$(BF): $(EIR) $(ELC)
	$(ELC) -bf $< > $@
	@echo ""
	@echo "============================================"
	@echo "  Brainfuck SDK compiled!"
	@echo "  Output: $(BF)"
	@echo "  Size: $$(wc -c < $(BF)) bytes"
	@echo "  Lines: $$(wc -l < $(BF))"
	@echo "============================================"

bf: $(BF)

run: $(BF)
	python3 runner.py --bf-interpreter $(BFOPT) --bf-program $(BF) --url $(FUSIONAUTH_URL) -v

# ---- Utilities ----

# Show file sizes at each compilation stage
sizes: $(SRC) $(EIR) $(BF)
	@echo "Compilation size progression:"
	@echo "  C source:      $$(wc -c < $(SRC)) bytes ($$(wc -l < $(SRC)) lines)"
	@echo "  ELVM IR:       $$(wc -c < $(EIR)) bytes ($$(wc -l < $(EIR)) lines)"
	@echo "  Brainfuck:     $$(wc -c < $(BF)) bytes"
	@echo "  Expansion ratio (C→BF): $$(echo "scale=0; $$(wc -c < $(BF)) / $$(wc -c < $(SRC))" | bc)x"

# Count only BF instruction characters (strip comments/whitespace)
bf-stats: $(BF)
	@echo "Brainfuck instruction statistics:"
	@echo "  Total file size: $$(wc -c < $(BF)) bytes"
	@echo "  > (move right):  $$(grep -o '>' $(BF) | wc -l)"
	@echo "  < (move left):   $$(grep -o '<' $(BF) | wc -l)"
	@echo "  + (increment):   $$(grep -o '+' $(BF) | wc -l)"
	@echo "  - (decrement):   $$(grep -o '\-' $(BF) | wc -l)"
	@echo "  . (output):      $$(grep -o '\.' $(BF) | wc -l)"
	@echo "  , (input):       $$(grep -o ',' $(BF) | wc -l)"
	@echo "  [ (loop start):  $$(grep -o '\[' $(BF) | wc -l)"
	@echo "  ] (loop end):    $$(grep -o '\]' $(BF) | wc -l)"

# Verify ELVM tools exist
check-elvm:
	@test -f $(ECC) || (echo "ERROR: ELVM 8cc not found at $(ECC)" && echo "Run: git clone https://github.com/shinh/elvm && cd elvm && make" && exit 1)
	@test -f $(ELC) || (echo "ERROR: ELVM elc not found at $(ELC)" && exit 1)
	@echo "ELVM toolchain: OK"

clean:
	rm -f $(NATIVE) $(EIR) $(BF)

.PHONY: native eir bf run test-native test-eir sizes bf-stats check-elvm clean