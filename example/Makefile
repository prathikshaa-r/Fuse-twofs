all:
	mkdir -p mountdir
	dd if=/dev/zero of=filesystem_image bs=1K count=2
distdir:
	cp Makefile $(distdir)

mostlyclean clean distclean mainainer-clean:
	rm -r filesystem_image
