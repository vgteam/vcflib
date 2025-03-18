/// This zig module handles VCF samples and genotypes

const std = @import("std");
const mem = std.mem;
const fmt = std.fmt;
const ArrayList = std.ArrayList;
const expect = std.testing.expect;
const expectEqual = std.testing.expectEqual;
const expectEqualSlices = std.testing.expectEqualSlices;
const eql = std.mem.eql;
const p = std.debug.print;
var gpa = std.heap.GeneralPurposeAllocator(.{}){};
const allocator = gpa.allocator();

const vcf = @import("vcf.zig");

const warning = vcf.warning;

pub const VcfSampleError = error{
    None,
    CannotParseSample,
    MultiAltSNPProblem,
    OutOfMemory
};

const GENOTYPE_MISSING = -256;

fn split_samples(str: []const u8) *ArrayList([] const u8) {
    var list = ArrayList([] const u8).init(allocator);
    defer list.deinit();
    var splits = std.mem.split(u8, str, " ");
    while (splits.next()) |chunk| {
        list.append(chunk) catch |err| {
            std.debug.print("out of memory {}\n", .{err});
        };
    }
    return &list;
}


/// Genotypes come as a per sample list of integers separated by |
/// (phased) or / (unphased).  You can't really mix phased/unphased so
/// we can track that as a boolean. To track samples create an
/// ArrayList(genotypes).
const Genotypes = struct {
    // default field values
    genos: ArrayList(i64), 
    phased: bool = false,

    const Self = @This();

    // state
    // var g_err: VcfSampleError = error.None;
    // ---- helpers

    fn is_phased(str: [] const u8) bool {
        return std.mem.containsAtLeast(u8, str, 1, "|");
    }
    
    /// Convert a genotype (sample) string to a list of numbers
    fn to_num(str: []const u8) !ArrayList(i64) {
        var list = ArrayList(i64).init(allocator);

        var splits = if (is_phased(str)) 
            std.mem.split(u8, str, "|")
        else
            std.mem.split(u8, str, "/");

        while (splits.next()) |chunk| {
            const i: i64 = 
                if (chunk[0] == '.') 
                GENOTYPE_MISSING
                else
                fmt.parseInt(i64,chunk,10) catch |err| {
                    try expect(err == error.InvalidCharacter);
                    return error.CannotParseSample;
                };
            list.append(i) catch unreachable;
        }
        return list;
    }

    fn init(str: [] const u8) Genotypes {
        const parsed = to_num(str) catch unreachable;
        return Genotypes {
            .genos = parsed,
            .phased = is_phased(str)
        };
    }
    
    fn deinit(self: *const Self) void {
        self.genos.deinit();
    }
    
    /// Take a 0-n indexed genotype and add offset idx. When the
    /// genotype is 0 (ref) or missing it is not changed.
    fn renumber(self: *const Self, idx: usize) !void {
        var list = self.genos;
        for (list.items,0..) | g,i | {
            list.items[i] = 
                switch (g) {
                    0 => 0,
                    GENOTYPE_MISSING => GENOTYPE_MISSING,
                    else => g+@as(i64,@intCast(idx))
            };
        }
    }

    /// Merge two ALT samples. If there is a conflict we select the
    /// last genotype. FIXME: at this point we are not checking for
    /// size mismatches
    fn merge(self: *const Self, genos2: Genotypes) !void {
        var base = self.genos;
        var g_err: VcfSampleError = error.None;
        
        for (genos2.genos.items,0..) | g2,i | {
            const current = base.items[i];
            if (g2 == 0 or g2 == GENOTYPE_MISSING) continue; // no update
            if (current>0) {
                try warning("Too many ALT alleles to fit in sample(s) - record marked with MULTI=ALTPROBLEM");
                g_err = error.MultiAltSNPProblem;
            }
            base.items[i] = g2;
        }
        return g_err;
    }
    
    fn to_s(self: *const Self) !ArrayList(u8) {
        const list = self.genos.items;
        const phase_repr = if (self.phased) "|" else "/";
        var s = ArrayList(u8).init(allocator);
        // concatenate genotypes with their phase separator
        for (list) | g | {
            const result = if (g == GENOTYPE_MISSING) 
                try fmt.allocPrint(allocator, ".{s}", .{phase_repr})
            else
                try fmt.allocPrint(allocator, "{}{s}", .{g,phase_repr});
            defer allocator.free(result);
            try s.appendSlice(result);
        }
        s.items = s.items[0..s.items.len-1]; // drop trailing phase character
        // p("*{s}*",.{s.items});
        return s;
    }
    
    fn to_s2(self: *const Self) !ArrayList([] const u8) {
        // _ = self;
        var s = ArrayList([] const u8).init(allocator);
        for (self.genos.items) |g| {
            // parseInt to go to str
            // charDigit to int
            const result = try fmt.allocPrint(allocator, "{}", .{g});
            // p("Result is {s}!\n", .{result});
            try s.append(result);
        }
        return s;
    }
};

const ReturnGenotypes = struct {
    g_err: VcfSampleError = error.None,
    s_samples: ArrayList([] const u8)
};

/// Walks all genotypes in the list of variants and reduces them to
/// the genotypes of the combined variant. For examples 0|1 genotypes
/// get translated to their list numbers 0|6. This works for
/// heterozygous only (at this point).
pub fn reduce_renumber_genotypes(comptime T: type, vs: ArrayList(T)) !ReturnGenotypes {
//pub fn reduce_renumber_genotypes(comptime T: type, vs: ArrayList(T)) !ArrayList([] const u8) {
    var samples = ArrayList(Genotypes).init(allocator); // result set
    var g_err: VcfSampleError = error.None;
    for (vs.items, 0..) | v,i | { // Fetch the genotypes from each variant
        for (v.genotypes().items, 0..) | geno,j | {
            var geno2 = Genotypes.init(geno); // convert from string to number list
            try geno2.renumber(i);
            if (i==0) {
                try samples.append(geno2);
            }
            else {
                samples.items[j].merge(geno2) catch |err| {
                    if (err != error.None)
                        g_err = err;
                };
            }
        }
    }
    // convert to zero terminated strings for vcflib C++ core code
    var s_samples = ArrayList([] const u8).init(allocator);
    for (samples.items) |g| {
        const s = try g.to_s();
        s_samples.append(s.items) catch unreachable;
    }
    // p("ngeno_s: size {d} {s}\n",.{s_samples.items.len,s_samples.items});
    // return s_samples;
    return ReturnGenotypes { .g_err = g_err, .s_samples = s_samples };
}

test "split genotypes" {
    const input_samples = "1|0 .|1 0|1 1|1";
    try std.testing.expectEqual(split_samples(input_samples).items.len, 4);
}

test "genotypes" {
    var list = ArrayList(i64).init(allocator);
    defer list.deinit();
    try list.append(0);
    try list.append(1);
    var gs = Genotypes{.genos = list, .phased = true};
    var genos = try gs.to_s2();
    defer {
        for (genos.items) |item| {
                allocator.free(item);
            }
        genos.deinit();
    }
    // p("YES {s}",.{genos.items});

    const gs2 = try Genotypes.to_num("1|0");
    defer gs2.deinit();
    try expect(gs2.items[0]==1);
    try expectEqual(gs2.items[1],0);
    const gs3 = Genotypes.init("1|.");
    defer gs3.deinit();
    const list2 = gs3.genos.items;
    try expectEqual(list2.len,2);
    try expectEqual(list2[1],GENOTYPE_MISSING);
    try gs3.renumber(1);
    // try expectEqual(add3.items[0],2);
    // try expectEqual(add3.items[1],GENOTYPE_MISSING);
    const s3 = try gs3.to_s();
    defer s3.deinit();
    try expect(eql(u8, s3.items, "2|."));
    const gs4 = try Genotypes.to_num(".|2");
    defer gs4.deinit();
    try expectEqualSlices(i64, gs4.items, &.{ GENOTYPE_MISSING, 2 });
    //const add4 = Genotypes.renumber(1,gs4);
    //try expectEqualSlices(i64, add4.items, &.{ GENOTYPE_MISSING, 3 });

    const genotypes = Genotypes.init("1|0");
    defer genotypes.deinit();
    // p("{d}",.{genotypes.genos.items});
    const str = try genotypes.to_s();
    defer str.deinit();
    // p("{s}",.{str});
    try expect(eql(u8, str.items, "1|0"));
}

