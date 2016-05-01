make patch file:
		diff -urN src_code src_code_new > changes.patch

use patch file:
		cd src_code
		patch -p1 < changes.patch
