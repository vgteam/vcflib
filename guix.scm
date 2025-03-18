;; To use this file to build HEAD of vcflib:
;;
;;   guix build -f guix.scm
;;
;; To get a development container (emacs shell will work)
;;
;;   guix shell -C -D -f guix.scm
;;
;; For the tests you need /usr/bin/env. In a container create it with
;;
;;   mkdir -p /usr/bin /bin ; ln -v -s $GUIX_ENVIRONMENT/bin/env /usr/bin/env ; ln -v -s $GUIX_ENVIRONMENT/bin/bash /bin/bash
;;
;; or in one go
;;
;;   guix shell -C -D -f guix.scm -- bash --init-file <(echo "mkdir -p /usr/bin && ln -s \$GUIX_ENVIRONMENT/bin/env /usr/bin/env && ln -v -s $GUIX_ENVIRONMENT/bin/bash /bin/bash && cd build")
;;
;;   cmake  -DCMAKE_BUILD_TYPE=Debug -DOPENMP=OFF -DASAN=ON ..
;;   make -j 12
;;   ctest .
;;
;; debug example
;;
;;   env LD_LIBRARY_PATH=$GUIX_ENVIRONMENT/lib gdb --args vcfallelicprimitives -m ../samples/10158243.vcf
;;
;; zig compiler
;;
;; To bring in a recent zig compiler I do something like
;;
;;   guix shell -C -D -f guix.scm --expose=/home/wrk/opt/zig-linux-x86_64-0.11.0-dev.987+a1d82352d/=/zig
;;
;; and add /zig to the PATH. E.g.
;;
;;   export PATH=/zig:$PATH

(use-modules
  ((guix licenses) #:prefix license:)
  (guix gexp)
  (guix packages)
  (guix git-download)
  (guix build-system cmake)
  (gnu packages algebra)
  (gnu packages autotools)
  (gnu packages base)
  (gnu packages compression)
  (gnu packages bioinformatics)
  (gnu packages build-tools)
  (gnu packages check)
  (gnu packages curl)
  (gnu packages gcc)
  (gnu packages gdb)
  (gnu packages haskell-xyz) ; pandoc for help files
  (gnu packages llvm)
  (gnu packages parallel)
  (gnu packages perl)
  (gnu packages perl6)
  (gnu packages pkg-config)
  (gnu packages python)
  (gnu packages python-xyz) ; for pybind11
  (gnu packages ruby)
  (gnu packages time)
  (gnu packages tls)
  ;; (gnu packages zig)
  (srfi srfi-1)
  (ice-9 popen)
  (ice-9 rdelim))

(define %source-dir (dirname (current-filename)))

(define %git-commit
    (read-string (open-pipe "git show HEAD | head -1 | cut -d ' ' -f 2" OPEN_READ)))

(define-public vcflib-git
  (package
    (name "vcflib-git")
    (version (git-version "1.0.10" "HEAD" %git-commit))
    (source (local-file %source-dir #:recursive? #t))
    (build-system cmake-build-system)
    (inputs
     `(("autoconf" ,autoconf)   ;; htslib build requirement
       ("automake" ,automake)   ;; htslib build requirement
       ("openssl" ,openssl)     ;; htslib build requirement
       ("curl" ,curl)           ;; htslib build requirement
       ("fastahack" ,fastahack) ;; dev version not in Debian
       ;; ("gcc" ,gcc-13)       ;; test against latest - won't build python bindings
       ("gdb" ,gdb)
       ("htslib" ,htslib)
       ("pandoc" ,pandoc)       ;; for generation man pages
       ("perl" ,perl)
       ("python" ,python)
       ("python-pytest" ,python-pytest)
       ("pybind11" ,pybind11)
       ("ruby" ,ruby)           ;; for generating man pages
       ("smithwaterman" ,smithwaterman) ;; dev version not in Debian
       ("tabixpp" ,tabixpp)
       ("time" ,time) ;; for tests
       ("wfa2-lib" ,wfa2-lib) ; alternative:  cmake  -DCMAKE_BUILD_TYPE=Debug -DWFA_GITMODULE=ON -DZIG=ON ..
       ("xz" ,xz)
       ;; ("zig" ,zig) ; for when zig gets up-to-date on Guix
       ("zlib" ,zlib)))
    (native-inputs
     `(("pkg-config" ,pkg-config)))
    (home-page "https://github.com/vcflib/vcflib/")
    (synopsis "Library for parsing and manipulating VCF files")
    (description "Vcflib provides methods to manipulate and interpret
sequence variation as it can be described by VCF.  It is both an API for parsing
and operating on records of genomic variation as it can be described by the VCF
format, and a collection of command-line utilities for executing complex
manipulations on VCF files.")
    (license license:expat)))

vcflib-git
