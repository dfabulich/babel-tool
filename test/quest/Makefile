# Test babel with Quest

include ../Makefile.boilerplate

.PHONY: $(CURDIR)
$(CURDIR):
	echo legacy: > output
	../../babel -format legacy.quest >> output
	../../babel -ifid legacy.quest >> output
	../../babel -identify legacy.quest >> output
	echo modern: >> output
	../../babel -format modern.quest >> output
	../../babel -ifid modern.quest >> output
	../../babel -identify modern.quest >> output
	../../babel -ifiction modern.quest >> output
	diff 1974A053-7DB0-4103-93A1-767C1382C0B7.iFiction expected-modern.iFiction >> output
	echo raw: >> output
	../../babel -format raw.aslx >> output
	../../babel -ifid raw.aslx >> output
	echo asl: >> output
	../../babel -format sample.asl >> output
	../../babel -ifid sample.asl >> output
	echo cas: >> output
	../../babel -format sample.cas >> output
	../../babel -ifid sample.cas >> output
	$(VERIFY)

clean:
	-rm -f output 1974A053-7DB0-4103-93A1-767C1382C0B7.iFiction 1974A053-7DB0-4103-93A1-767C1382C0B7.png
