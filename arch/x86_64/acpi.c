/*
 * ACPI table discovery.
 *
 * The bootloader gives us the RSDP ("root pointer"), which points at the
 * XSDT (or the older 32-bit RSDT): an array of physical addresses of all
 * other tables. Every table starts with the same header, carrying a 4-byte
 * signature and a length. We need two tables:
 *   "APIC" (MADT)  interrupt controllers and IRQ wiring
 *   "FACP" (FADT)  among much else, the I/O port of the ACPI PM timer
 *
 * The tables live in firmware memory outside the direct map, so each is
 * mapped on demand. Mappings are never removed; they total a few pages.
 */
#include "acpi.h"

#include <stddef.h>

#include "boot/boot.h"

struct rsdp {
    char signature[8]; /* "RSD PTR " */
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision; /* 0 = ACPI 1.0 (RSDT only), 2+ = XSDT available */
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

struct sdt_header {
    char signature[4];
    uint32_t length; /* whole table, header included */
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct madt {
    struct sdt_header header;
    uint32_t lapic_address;
    uint32_t flags;
    /* followed by variable-length entries */
} __attribute__((packed));

#define MADT_FLAG_PCAT_COMPAT 1u /* the machine also has a legacy 8259 PIC */

struct madt_entry_header {
    uint8_t type;
    uint8_t length;
} __attribute__((packed));

#define MADT_ENTRY_IOAPIC         1
#define MADT_ENTRY_IRQ_OVERRIDE   2
#define MADT_ENTRY_LAPIC_OVERRIDE 5

struct madt_ioapic {
    struct madt_entry_header header;
    uint8_t id;
    uint8_t reserved;
    uint32_t address;
    uint32_t gsi_base;
} __attribute__((packed));

struct madt_irq_override {
    struct madt_entry_header header;
    uint8_t bus;
    uint8_t source;
    uint32_t gsi;
    uint16_t flags;
} __attribute__((packed));

/* Polarity in bits 0-1, trigger mode in bits 2-3; the value 3 means
 * "active low" / "level triggered", 0 and 1 mean the ISA default. */
#define OVERRIDE_POLARITY(flags) ((flags) & 0x3)
#define OVERRIDE_TRIGGER(flags)  (((flags) >> 2) & 0x3)
#define OVERRIDE_NON_DEFAULT     3

struct madt_lapic_override {
    struct madt_entry_header header;
    uint16_t reserved;
    uint64_t address;
} __attribute__((packed));

/* Byte offset of the 32-bit PM timer port in the FADT. */
#define FADT_PM_TIMER_BLOCK_OFFSET 76

static arch_map_device_fn map_device;

static bool signature_matches(const char *actual, const char *expected, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        if (actual[i] != expected[i]) {
            return false;
        }
    }
    return true;
}

/* ACPI checksums are chosen so that all bytes of the structure sum to 0. */
static bool checksum_valid(const void *data, size_t length)
{
    const uint8_t *bytes = data;
    uint8_t sum = 0;
    for (size_t i = 0; i < length; i++) {
        sum += bytes[i];
    }
    return sum == 0;
}

/* Maps a whole table. Its length is only known once the header is mapped,
 * so this maps twice. */
static const struct sdt_header *map_table(uint64_t address)
{
    const struct sdt_header *header = map_device(address, sizeof(*header));
    if (header == NULL) {
        return NULL;
    }
    const struct sdt_header *table = map_device(address, header->length);
    if (table == NULL || !checksum_valid(table, table->length)) {
        return NULL;
    }
    return table;
}

/*
 * The root table is a header followed by an array of table addresses:
 * 8 bytes each in the XSDT, 4 bytes each in the RSDT. The array is not
 * aligned, hence the byte-wise read.
 */
static uint64_t root_entry(const struct sdt_header *root, size_t index, size_t entry_size)
{
    const uint8_t *entry = (const uint8_t *)root + sizeof(*root) + index * entry_size;
    uint64_t address = 0;
    for (size_t i = 0; i < entry_size; i++) {
        address |= (uint64_t)entry[i] << (8 * i);
    }
    return address;
}

static const struct sdt_header *find_table(const struct sdt_header *root, size_t entry_size, const char *signature)
{
    size_t count = (root->length - sizeof(*root)) / entry_size;

    for (size_t i = 0; i < count; i++) {
        const struct sdt_header *header = map_device(root_entry(root, i, entry_size), sizeof(*header));
        if (header != NULL && signature_matches(header->signature, signature, 4)) {
            return map_table(root_entry(root, i, entry_size));
        }
    }
    return NULL;
}

static void add_override(struct acpi_info *info, const struct madt_irq_override *entry)
{
    if (info->override_count == ACPI_MAX_IRQ_OVERRIDES) {
        return;
    }
    info->overrides[info->override_count++] = (struct acpi_irq_override){
        .source = entry->source,
        .gsi = entry->gsi,
        .active_low = OVERRIDE_POLARITY(entry->flags) == OVERRIDE_NON_DEFAULT,
        .level_triggered = OVERRIDE_TRIGGER(entry->flags) == OVERRIDE_NON_DEFAULT,
    };
}

static void parse_madt(const struct madt *madt, struct acpi_info *info)
{
    info->lapic_address = madt->lapic_address;
    info->has_legacy_pic = (madt->flags & MADT_FLAG_PCAT_COMPAT) != 0;

    const uint8_t *cursor = (const uint8_t *)madt + sizeof(*madt);
    const uint8_t *end = (const uint8_t *)madt + madt->header.length;

    while (cursor + sizeof(struct madt_entry_header) <= end) {
        const struct madt_entry_header *entry = (const void *)cursor;
        if (entry->length < sizeof(*entry)) {
            break; /* malformed table; a zero length would loop forever */
        }
        switch (entry->type) {
        case MADT_ENTRY_IOAPIC: {
            const struct madt_ioapic *ioapic = (const void *)cursor;
            /* Only the first I/O APIC is used; it serves all legacy IRQs. */
            if (info->ioapic_address == 0) {
                info->ioapic_address = ioapic->address;
                info->ioapic_gsi_base = ioapic->gsi_base;
            }
            break;
        }
        case MADT_ENTRY_IRQ_OVERRIDE:
            add_override(info, (const void *)cursor);
            break;
        case MADT_ENTRY_LAPIC_OVERRIDE:
            info->lapic_address = ((const struct madt_lapic_override *)(const void *)cursor)->address;
            break;
        default:
            break;
        }
        cursor += entry->length;
    }
}

static void parse_fadt(const struct sdt_header *fadt, struct acpi_info *info)
{
    if (fadt->length < FADT_PM_TIMER_BLOCK_OFFSET + sizeof(uint32_t)) {
        return;
    }
    const uint8_t *field = (const uint8_t *)fadt + FADT_PM_TIMER_BLOCK_OFFSET;
    uint32_t port = field[0] | (field[1] << 8) | ((uint32_t)field[2] << 16) | ((uint32_t)field[3] << 24);
    info->pm_timer_port = (uint16_t)port;
}

const char *acpi_init(arch_map_device_fn map, struct acpi_info *info)
{
    map_device = map;
    *info = (struct acpi_info){ 0 };

    uint64_t rsdp_address = boot_rsdp_address();
    if (rsdp_address == 0) {
        return "bootloader did not provide the ACPI root pointer";
    }
    const struct rsdp *rsdp = map_device(rsdp_address, sizeof(*rsdp));
    if (rsdp == NULL || !signature_matches(rsdp->signature, "RSD PTR ", 8)) {
        return "ACPI root pointer is invalid";
    }

    bool use_xsdt = rsdp->revision >= 2 && rsdp->xsdt_address != 0;
    const struct sdt_header *root = map_table(use_xsdt ? rsdp->xsdt_address : rsdp->rsdt_address);
    if (root == NULL) {
        return "ACPI root table is invalid";
    }
    size_t entry_size = use_xsdt ? sizeof(uint64_t) : sizeof(uint32_t);

    const struct sdt_header *madt = find_table(root, entry_size, "APIC");
    if (madt == NULL) {
        return "ACPI MADT (interrupt controller table) not found";
    }
    parse_madt((const struct madt *)madt, info);
    if (info->lapic_address == 0 || info->ioapic_address == 0) {
        return "ACPI MADT does not describe a local APIC and an I/O APIC";
    }

    const struct sdt_header *fadt = find_table(root, entry_size, "FACP");
    if (fadt != NULL) {
        parse_fadt(fadt, info);
    }
    return NULL;
}
