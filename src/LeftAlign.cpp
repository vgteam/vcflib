#include "LeftAlign.hpp"

// Attempts to left-realign all the indels represented by the alignment cigar.
//
// This is done by shifting all indels as far left as they can go without
// mismatch, then merging neighboring indels of the same class.  leftAlign
// updates the alignment cigar with changes, and returns true if realignment
// changed the alignment cigar.
//
// To left-align, we move multi-base indels left by their own length as long as
// the preceding bases match the inserted or deleted sequence.  After this
// step, we handle multi-base homopolymer indels by shifting them one base to
// the left until they mismatch the reference.
//
// To merge neighboring indels, we iterate through the set of left-stabilized
// indels.  For each indel we add a new cigar element to the new cigar.  If a
// deletion follows a deletion, or an insertion occurs at the same place as
// another insertion, we merge the events by extending the previous cigar
// element.
//
// In practice, we must call this function until the alignment is stabilized.

#define VCFLEFTALIGN_DEBUG(msg) \
    if (false) { cerr << msg; }

namespace vcflib {

bool VCFIndelAllele::homopolymer(void) {
    string::iterator s = sequence.begin();
    char c = *s++;
    while (s != sequence.end()) {
        if (c != *s++) return false;
    }
    return true;
}

bool FBhomopolymer(string sequence) {
    string::iterator s = sequence.begin();
    char c = *s++;
    while (s != sequence.end()) {
        if (c != *s++) return false;
    }
    return true;
}

ostream& operator<<(ostream& out, const VCFIndelAllele& indel) {
    string t = indel.insertion ? "i" : "d";
    out << t <<  ":" << indel.position << ":" << indel.readPosition << ":" << indel.sequence;
    return out;
}

bool operator==(const VCFIndelAllele& a, const VCFIndelAllele& b) {
    return (a.insertion == b.insertion
            && a.length == b.length
            && a.position == b.position
            && a.sequence == b.sequence);
}

bool operator!=(const VCFIndelAllele& a, const VCFIndelAllele& b) {
    return !(a==b);
}

bool operator<(const VCFIndelAllele& a, const VCFIndelAllele& b) {
    ostringstream as, bs;
    as << a;
    bs << b;
    return as.str() < bs.str();
}

double entropy(const string& st) {
    vector<char> stvec(st.begin(), st.end());
    set<char> alphabet(stvec.begin(), stvec.end());
    vector<double> freqs;
    for (set<char>::iterator c = alphabet.begin(); c != alphabet.end(); ++c) {
        int ctr = 0;
        for (vector<char>::iterator s = stvec.begin(); s != stvec.end(); ++s) {
            if (*s == *c) {
                ++ctr;
            }
        }
        freqs.push_back((double)ctr / (double)stvec.size());
    }
    double ent = 0;
    double ln2 = log(2);
    for (vector<double>::iterator f = freqs.begin(); f != freqs.end(); ++f) {
        ent += *f * log(*f)/ln2;
    }
    ent = -ent;
    return ent;
}

bool leftAlign(string& alternateSequence, Cigar& cigar, string& referenceSequence, bool debug) {

    int arsOffset = 0; // pointer to insertion point in aligned reference sequence
    string alignedReferenceSequence = referenceSequence;
    int aabOffset = 0;
    string alignmentAlignedBases = alternateSequence;

    // store information about the indels
    vector<VCFIndelAllele> indels;

    int rp = 0;  // read position, 0-based relative to read
    int sp = 0;  // sequence position

    string softBegin;
    string softEnd;

    stringstream cigar_before, cigar_after;
    for (vector<pair<int, char> >::const_iterator c = cigar.begin();
        c != cigar.end(); ++c) {
        unsigned int l = c->first;
        char t = c->second;

        cigar_before << l << t;
        if (t == 'M') { // match or mismatch
            sp += l;
            rp += l;
        } else if (t == 'D') { // deletion
            indels.push_back(VCFIndelAllele(false, l, sp, rp, referenceSequence.substr(sp, l)));
            alignmentAlignedBases.insert(rp + aabOffset, string(l, '-'));
            aabOffset += l;
            sp += l;  // update reference sequence position
        } else if (t == 'I') { // insertion
            indels.push_back(VCFIndelAllele(true, l, sp, rp, alternateSequence.substr(rp, l)));
            alignedReferenceSequence.insert(sp + softBegin.size() + arsOffset, string(l, '-'));
            arsOffset += l;
            rp += l;
        } else if (t == 'S') { // soft clip, clipped sequence present in the read not matching the reference
            // remove these bases from the refseq and read seq, but don't modify the alignment sequence
            if (rp == 0) {
                alignedReferenceSequence = string(l, '*') + alignedReferenceSequence;
                softBegin = alignmentAlignedBases.substr(0, l);
            } else {
                alignedReferenceSequence = alignedReferenceSequence + string(l, '*');
                softEnd = alignmentAlignedBases.substr(alignmentAlignedBases.size() - l, l);
            }
            rp += l;
        } else if (t == 'H') { // hard clip on the read, clipped sequence is not present in the read
        } else if (t == 'N') { // skipped region in the reference not present in read, aka splice
            sp += l;
        }
    }


    int alignedLength = sp;

    VCFLEFTALIGN_DEBUG("| " << cigar_before.str() << endl
       << "| " << alignedReferenceSequence << endl
       << "| " << alignmentAlignedBases << endl);

    // if no indels, return the alignment
    if (indels.empty()) { return false; }

    // for each indel, from left to right
    //     while the indel sequence repeated to the left and we're not matched up with the left-previous indel
    //         move the indel left

    vector<VCFIndelAllele>::iterator previous = indels.begin();
    for (vector<VCFIndelAllele>::iterator id = indels.begin(); id != indels.end(); ++id) {

        // left shift by repeats
        //
        // from 1 base to the length of the indel, attempt to shift left
        // if the move would cause no change in alignment optimality (no
        // introduction of mismatches, and by definition no change in gap
        // length), move to the new position.
        // in practice this moves the indel left when we reach the size of
        // the repeat unit.
        //
        int steppos, readsteppos;
        VCFIndelAllele& indel = *id;
        int i = 1;
        while (i <= indel.length) {

            int steppos = indel.position - i;
            int readsteppos = indel.readPosition - i;

#ifdef VERBOSE_DEBUG
            if (debug) {
                if (steppos >= 0 && readsteppos >= 0) {
                    cerr << referenceSequence.substr(steppos, indel.length) << endl;
                    cerr << alternateSequence.substr(readsteppos, indel.length) << endl;
                    cerr << indel.sequence << endl;
                }
            }
#endif
            while (steppos >= 0 && readsteppos >= 0
                   && indel.sequence == referenceSequence.substr(steppos, indel.length)
                   && indel.sequence == alternateSequence.substr(readsteppos, indel.length)
                   && (id == indels.begin()
                       || (previous->insertion && steppos >= previous->position)
                       || (!previous->insertion && steppos >= previous->position + previous->length))) {
                VCFLEFTALIGN_DEBUG((indel.insertion ? "insertion " : "deletion ") << indel << " shifting " << i << "bp left" << endl);
                indel.position -= i;
                indel.readPosition -= i;
                steppos = indel.position - i;
                readsteppos = indel.readPosition - i;
            }
            do {
                ++i;
            } while (i <= indel.length && indel.length % i != 0);
        }

        // left shift indels with exchangeable flanking sequence
        //
        // for example:
        //
        //    GTTACGTT           GTTACGTT
        //    GT-----T   ---->   G-----TT
        //
        // GTGTGACGTGT           GTGTGACGTGT
        // GTGTG-----T   ---->   GTG-----TGT
        //
        // GTGTG-----T           GTG-----TGT
        // GTGTGACGTGT   ---->   GTGTGACGTGT
        //
        //
        steppos = indel.position - 1;
        readsteppos = indel.readPosition - 1;
        while (steppos >= 0 && readsteppos >= 0
               && alternateSequence.at(readsteppos) == referenceSequence.at(steppos)
               && alternateSequence.at(readsteppos) == indel.sequence.at(indel.sequence.size() - 1)
               && (id == indels.begin()
                   || (previous->insertion && indel.position - 1 >= previous->position)
                   || (!previous->insertion && indel.position - 1 >= previous->position + previous->length))) {
            VCFLEFTALIGN_DEBUG((indel.insertion ? "insertion " : "deletion ") << indel << " exchanging bases " << 1 << "bp left" << endl);
            indel.sequence = indel.sequence.at(indel.sequence.size() - 1) + indel.sequence.substr(0, indel.sequence.size() - 1);
            indel.position -= 1;
            indel.readPosition -= 1;
            steppos = indel.position - 1;
            readsteppos = indel.readPosition - 1;
        }
        // tracks previous indel, so we don't run into it with the next shift
        previous = id;
    }

    // bring together floating indels
    // from left to right
    // check if we could merge with the next indel
    // if so, adjust so that we will merge in the next step
    if (indels.size() > 1) {
        previous = indels.begin();
        for (vector<VCFIndelAllele>::iterator id = (indels.begin() + 1); id != indels.end(); ++id) {
            VCFIndelAllele& indel = *id;
            // parsimony: could we shift right and merge with the previous indel?
            // if so, do it
            int prev_end_ref = previous->insertion ? previous->position : previous->position + previous->length;
            int prev_end_read = !previous->insertion ? previous->readPosition : previous->readPosition + previous->length;
            if (previous->insertion == indel.insertion
                    && ((previous->insertion
                        && (previous->position < indel.position
                        && previous->readPosition + previous->readPosition < indel.readPosition))
                        ||
                        (!previous->insertion
                        && (previous->position + previous->length < indel.position)
                        && (previous->readPosition < indel.readPosition)
                        ))) {
                if (previous->homopolymer()) {
                    string seq = referenceSequence.substr(prev_end_ref, indel.position - prev_end_ref);
                    string readseq = alternateSequence.substr(prev_end_read, indel.position - prev_end_ref);
                    VCFLEFTALIGN_DEBUG("seq: " << seq << endl << "readseq: " << readseq << endl);
                    if (previous->sequence.at(0) == seq.at(0)
                            && FBhomopolymer(seq)
                            && FBhomopolymer(readseq)) {
                        VCFLEFTALIGN_DEBUG("moving " << *previous << " right to "
                                << (indel.insertion ? indel.position : indel.position - previous->length) << endl);
                        previous->position = indel.insertion ? indel.position : indel.position - previous->length;
                    }
                }
                else {
                    int pos = previous->position;
                    while (pos < (int) referenceSequence.length() &&
                            ((previous->insertion && pos + previous->length <= indel.position)
                            ||
                            (!previous->insertion && pos + previous->length < indel.position))
                            && previous->sequence
                                == referenceSequence.substr(pos + previous->length, previous->length)) {
                        pos += previous->length;
                    }
                    if (pos < previous->position &&
                        ((previous->insertion && pos + previous->length == indel.position)
                        ||
                        (!previous->insertion && pos == indel.position - previous->length))
                       ) {
                        VCFLEFTALIGN_DEBUG("right-merging tandem repeat: moving " << *previous << " right to " << pos << endl);
                        previous->position = pos;
                    }
                }
            }
            previous = id;
        }
    }

    // for each indel
    //     if ( we're matched up to the previous insertion (or deletion)
    //          and it's also an insertion or deletion )
    //         merge the indels
    //
    // and simultaneously reconstruct the cigar

    Cigar newCigar;

    if (!softBegin.empty()) {
        newCigar.push_back(make_pair(softBegin.size(), 'S'));
    }

    vector<VCFIndelAllele>::iterator id = indels.begin();
    VCFIndelAllele last = *id++;
    if (last.position > 0) {
        newCigar.push_back(make_pair(last.position, 'M'));
        newCigar.push_back(make_pair(last.length, (last.insertion ? 'I' : 'D')));
    } else {
        newCigar.push_back(make_pair(last.length, (last.insertion ? 'I' : 'D')));
    }
    int lastend = last.insertion ? last.position : (last.position + last.length);
    VCFLEFTALIGN_DEBUG(last << ",");

    for (; id != indels.end(); ++id) {
        VCFIndelAllele& indel = *id;
        VCFLEFTALIGN_DEBUG(indel << ",");
        if (indel.position < lastend) {
            cerr << "impossibility?: indel realigned left of another indel" << endl
                 << referenceSequence << endl << alternateSequence << endl;
            exit(1);
        } else if (indel.position == lastend && indel.insertion == last.insertion) {
            pair<int, char>& op = newCigar.back();
            op.first += indel.length;
        } else if (indel.position >= lastend) {  // also catches differential indels, but with the same position
            newCigar.push_back(make_pair(indel.position - lastend, 'M'));
            newCigar.push_back(make_pair(indel.length, (indel.insertion ? 'I' : 'D')));
        }
        last = *id;
        lastend = last.insertion ? last.position : (last.position + last.length);
    }

    if (lastend < alignedLength) {
        newCigar.push_back(make_pair(alignedLength - lastend, 'M'));
    }

    if (!softEnd.empty()) {
        newCigar.push_back(make_pair(softEnd.size(), 'S'));
    }

    VCFLEFTALIGN_DEBUG(endl);

    cigar = newCigar;

    for (vector<pair<int, char> >::const_iterator c = cigar.begin();
        c != cigar.end(); ++c) {
        unsigned int l = c->first;
        char t = c->second;
        cigar_after << l << t;
    }

    //cerr << cigar_before.str() << " changes to " << cigar_after.str() << endl;
    VCFLEFTALIGN_DEBUG(cigar_after.str() << endl);

    // check if we're realigned
    if (cigar_after.str() == cigar_before.str()) {
        return false;
    } else {
        return true;
    }

}

// Iteratively left-aligns the indels in the alignment until we have a stable
// realignment.  Returns true on realignment success or non-realignment.
// Returns false if we exceed the maximum number of realignment iterations.
//
bool stablyLeftAlign(string& alternateSequence, string referenceSequence, Cigar& cigar, int maxiterations, bool debug) {

    if (!leftAlign(alternateSequence, cigar, referenceSequence, debug)) {

        return true;

    } else {

        bool result = true;
        while ((result = leftAlign(alternateSequence, cigar, referenceSequence, debug)) && --maxiterations > 0) {
        }

        if (maxiterations <= 0) {
            return false;
        } else {
            return true;
        }

    }

}

}
