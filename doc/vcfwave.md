% VCFWAVE(1) vcfwave (vcflib) | vcfwave (VCF transformation)
% Erik Garrison, Pjotr Prins and other vcflib contributors

# NAME

vcfwave - reduces complex alleles by pairwise alignment with BiWFA

# SYNOPSIS

**vcfwave**

# DESCRIPTION

**vcfwave** reduces complex alleles into simpler primitive representation using pairwise
alignment with BiWFA.

Often variant callers are not perfect. **vcfwave** with its companion tool **vcfcreatemulti** can take an existing VCF file that contains multiple complex overlapping and even nested alleles and, like Humpty Dumpty, can take them apart and put them together again in a more sane VCF output.
Thereby getting rid of false positives and often greatly simplifying the output.
We created these tools for the output from long-read pangenome genotypers - with 10K base pair realignments - and is used in the Human Pangenome Reference Consortium analyses (HPRC).

**vcfwave** realigns reference and alternate alleles with the recently introduced super fast bi-wavefront aligner (WFA).
**vcfwave** parses out the original `primitive' alleles into multiple VCF records and **vcfcreatemulti** puts them together again.
These tools can handle insertions, deletions, inversions and nested sequences.
In both tools information is tracked on original positions and genotypes are handled.
New records have IDs that reference the source record ID.
Deletion alleles will result in haploid (missing allele) genotypes overlapping the deleted region.

A typical workflow will call **vcfwave** to realign all ALT alleles against the reference and spit out simplified VCF records.
Next use a tool such as `bcftools norm -m-` to normalise the VCF records and split out multiple ALT alleles into separate VCF records.
Finally use **vcfcreatemulti** to create multi-allele VCF records again.

PERFORMANCE:

Unlike traditional aligners that run in quadratic time, the recently introduced wavefront aligner WFA runs in time O(ns+s^2), proportional to the sequence length n and the alignment score s, using O(s^2) memory (or O(s) using the ultralow/BiWFA mode). Therefore WFA does not choke on longer alignments.

Speed-wise vcfwave can still be faster. See also the [performance docs](../test/doc/performance.md) for some metrics and discussion.

READING:

See also the *humpty dumpty* companion tool [vcfcreatemulti](./vcfcreatemulti.md).

## Options

-h, --help

: shows help message and exits.

See more below.

# EXIT VALUES

**0**
: Success

**not 0**
: Failure

# EXAMPLES


<!--

    >>> from rtest import run_stdout, head, cat, sh

-->

Current command line options:

```

>>> head("vcfwave -h",26)
>
usage: vcfwave [options] [file]
>
Realign reference and alternate alleles with WFA, parsing out the
'primitive' alleles into multiple VCF records. New records have IDs that
reference the source record ID.  Genotypes/samples are handled
correctly. Deletions generate haploid/missing genotypes at overlapping
sites.
>
options:
    -p, --wf-params PARAMS  use the given BiWFA params (default: 0,19,39,3,81,1)
                            format=match,mismatch,gap1-open,gap1-ext,gap2-open,gap2-ext
    -f, --tag-parsed FLAG   Annotate decomposed records with the source record position
                            (default: ORIGIN).
    -L, --max-length LEN    Do not manipulate records in which either the ALT or
                            REF is longer than LEN (default: unlimited).
    -K, --inv-kmer K        Length of k-mer to use for inversion detection sketching (default: 17).
    -I, --inv-min LEN       Minimum allele length to consider for inverted alignment (default: 64).
    -t, --threads N         Use this many threads for variant decomposition (default is 1).
                            For most datasets threading may actually slow vcfwave down.
    --quiet                 Do not display progress bar.
    -d, --debug             Debug mode.
>
Note the -k,--keep-info switch is no longer in use and ignored.
>
Type: transformation


```

vcfwave picks complex regions and simplifies nested alignments. For example:

```python

>>> sh("grep 10158243 ../samples/10158243.vcf")
grch38#chr4     10158243        >3655>3662      ACCCCCACCCCCACC ACC,AC,ACCCCCACCCCCAC,ACCCCCACC,ACA     60      .       AC=64,3,2,3,1;AF=0.719101,0.0337079,0.0224719,0.0337079,0.011236;AN=89;AT=>3655>3656>3657>3658>3659>3660>3662,>3655>3656>3660>3662,>3655>3660>3662,>3655>3656>3657>3658>3660>3662,>3655>3656>3657>3660>3662,>3655>3656>3661>3662;NS=45;LV=0     GT      0|0     1|1     1|1     1|0     5|1     0|4     0|1     0|1     1|1     1|1     1|1     1|1     1|1     1|1     1|1     4|3     1|1     1|1     1|1     1|0     1|0     1|0     1|0     1|1     1|1     1|4     1|1     1|1     3|0     1|0     1|1     0|1     1|1     1|1     2|1     1|2     1|1     1|1     0|1     1|1     1|1     1|0     1|2     1|1     0

```

This aligns and adjusts the genotypes accordingly splitting into multiple records, one for each unique allele found in the alignments:

```python

>>> sh("../build/vcfwave -L 1000 ../samples/10158243.vcf|grep -v ^\#")
grch38#chr4     10158244        >3655>3662_1    CCCCCACCCCCAC   C       60      .       AC=1;AF=0.011236;AN=89;AT=>3655>3656>3657>3660>3662;NS=45;LV=0;ORIGIN=grch38#chr4:10158243;LEN=12;TYPE=del        GT      0|0     0|0     0|0     0|0     1|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0
grch38#chr4     10158244        >3655>3662_2    CCCCCACCCCCACC  C       60      .       AC=3;AF=0.033708;AN=89;AT=>3655>3656>3660>3662;NS=45;LV=0;ORIGIN=grch38#chr4:10158243;LEN=13;TYPE=del     GT      0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     1|0     0|1     0|0     0|0     0|0     0|0     0|0     0|0     0|1     0|0     0
grch38#chr4     10158245        >3655>3662_3    CCCCACCCCCACC   C       60      .       AC=64;AF=0.719101;AN=89;AT=>3655>3656>3657>3658>3659>3660>3662;NS=45;LV=0;ORIGIN=grch38#chr4:10158243;LEN=12;TYPE=del     GT      0|0     1|1     1|1     1|0     0|1     0|0     0|1     0|1     1|1     1|1     1|1     1|1     1|1     1|1     1|1     0|0     1|1     1|1     1|1     1|0     1|0     1|0     1|0     1|1     1|1     1|0     1|1     1|1     0|0     1|0     1|1     0|1     1|1     1|1     0|1     1|0     1|1     1|1     0|1     1|1     1|1     1|0     1|0     1|1     0
grch38#chr4     10158251        >3655>3662_4    CCCCACC C       60      .       AC=3;AF=0.033708;AN=89;AT=>3655>3656>3657>3658>3660>3662;NS=45;LV=0;ORIGIN=grch38#chr4:10158243;LEN=6;TYPE=del    GT      0|0     0|0     0|0     0|0     0|0     0|1     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     1|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|1     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0
grch38#chr4     10158256        >3655>3662_5    CC      C       60      .       AC=2;AF=0.022472;AN=89;AT=>3655>3660>3662;NS=45;LV=0;ORIGIN=grch38#chr4:10158243;LEN=1;TYPE=del   GT      0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|1     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     1|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0|0     0
grch38#chr4     10158257        >3655>3662_6    C       A       60      .       AC=1;AF=0.011236;AN=89;AT=>3655>3656>3657>3660>3662;NS=45;LV=0;ORIGIN=grch38#chr4:10158243;LEN=1;TYPE=snp GT      0|0     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     .|.     0


```

## Source code

[vcfwave.cpp](../../src/vcfwave.cpp)

## Regression tests

The bidirectional wavefront (BiWFA) version has no problem with longer sequences (10_000bps is almost instant):

```python
# ./vcfwave -L 10000 ../samples/grch38#chr8_36353854-36453166.vcf > ../test/data/regression/vcfwave_4.vcf
>>> run_stdout("vcfwave -L 10000 ../samples/grch38#chr8_36353854-36453166.vcf", ext="vcf")
output in <a href="../data/regression/vcfwave_4.vcf">vcfwave_4.vcf</a>

# ./vcfwave -L 10000 ../samples/grch38#chr4_10083863-10181258.vcf > ../test/data/regression/vcfwave_5.vcf
>>> run_stdout("vcfwave -L 10000 ../samples/grch38#chr4_10083863-10181258.vcf", ext="vcf")
output in <a href="../data/regression/vcfwave_5.vcf">vcfwave_5.vcf</a>

```

## Inversions

We can also handle inversions.
This test case includes one that was introduced by building a variation graph with an inversion and then decomposing it into a VCF with `vg deconstruct` and finally "popping" the inversion variant with [`vcfbub`](https://github.com/pangenome/vcfbub).

From

```
a       281     >1>9    AGCCGGGGCAGAAAGTTCTTCCTTGAATGTGGTCATCTGCATTTCAGCTCAGGAATCCTGCAAAAGACAG  CTGTCTTTTGCAGGATTCCTGTGCTGAAATGCAGATGACCGCATTCAAGGAAGAACTATCTGCCCCGGCT     60      .       AC=1;AF=1;AN=1;AT=>1>2>3>4>5>6>7>8>9,>1<8>10<6>11<4>12<2>9;NS=1;LV=0       GT      1

```

To

```python
>>> sh("../build/vcfwave ../samples/inversion.vcf|grep INV")
##INFO=<ID=INV,Number=0,Type=Flag,Description="Inversion detected">
a       281     >1>9    AGCCGGGGCAGAAAGTTCTTCCTTGAATGTGGTCATCTGCATTTCAGCTCAGGAATCCTGCAAAAGACAG  CTGTCTTTTGCAGGATTCCTGTGCTGAAATGCAGATGACCGCATTCAAGGAAGAACTATCTGCCCCGGCT  60      .       AC=1;AF=1.000000;AN=1;AT=>1>2>3>4>5>6>7>8>9;NS=1;LV=0;LEN=70;INV=YES;TYPE=mnp  GT      1

```

Note the `INV=YES' info.

# LICENSE

Copyright 2022-2024 (C) Erik Garrison, Pjotr Prins and vcflib contributors. MIT licensed.
