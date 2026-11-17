#include <sqlite3ext.h>
SQLITE_EXTENSION_INIT1

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RCS_MAX_ARCHIVE (16 * 1024 * 1024)
#define RCS_MAX_CONTENT (8 * 1024 * 1024)
#define RCS_MAX_LOG (1024 * 1024)
#define RCS_MAX_REVISIONS 10000
#define RCS_MAX_SYMBOL_NAME 128
#define RCS_MAX_REVISION_COMPONENTS 32
#define RCS_MAX_DIFF_CELLS 4000000
#define RCS_MAX_DELTA_OPERATIONS 100000

typedef struct Buffer {
    char *data;
    size_t length;
    size_t capacity;
} Buffer;

typedef struct Revision {
    char *date;
    char *author;
    char *state;
    char *branches;
    int next_revision;
    int next_seen;
    char *log;
    size_t log_length;
    char *text;
    size_t text_length;
} Revision;

typedef struct Symbol {
    char *name;
    char *revision;
    int is_branch;
} Symbol;

typedef struct Branch {
    char *number;
    char *parent_revision;
    int base_revision;
    int branch_id;
    int count;
    int capacity;
    Revision *revisions;
} Branch;

typedef struct Archive {
    int count;
    Revision *revisions;
    int symbol_count;
    int symbol_capacity;
    Symbol *symbols;
    int branch_count;
    int branch_capacity;
    Branch *branches;
} Archive;

typedef struct TextLine {
    const char *data;
    size_t length;
} TextLine;

typedef struct MergeChange {
    int start;
    int end;
    int replacement_start;
    int replacement_count;
} MergeChange;

typedef struct DeltaOperation {
    char kind;
    int position;
    int count;
    int sequence;
    size_t added_start;
    size_t added_length;
} DeltaOperation;

static void set_error(sqlite3_context *context, const char *message)
{
    sqlite3_result_error(context, message, -1);
}

static int buffer_reserve(Buffer *buffer, size_t extra)
{
    size_t needed;
    size_t capacity;
    char *data;

    if (extra > (size_t)INT_MAX - buffer->length - 1) {
        return 0;
    }
    needed = buffer->length + extra + 1;
    if (needed <= buffer->capacity) {
        return 1;
    }
    capacity = buffer->capacity == 0 ? 256 : buffer->capacity;
    while (capacity < needed) {
        if (capacity > (size_t)INT_MAX / 2) {
            capacity = needed;
            break;
        }
        capacity *= 2;
    }
    data = (char *)sqlite3_realloc64(buffer->data, (sqlite3_uint64)capacity);
    if (data == NULL) {
        return 0;
    }
    buffer->data = data;
    buffer->capacity = capacity;
    return 1;
}

static int buffer_append(Buffer *buffer, const char *data, size_t length)
{
    if (!buffer_reserve(buffer, length)) {
        return 0;
    }
    if (length != 0) {
        memcpy(buffer->data + buffer->length, data, length);
    }
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return 1;
}

static int buffer_append_cstr(Buffer *buffer, const char *text)
{
    return buffer_append(buffer, text, strlen(text));
}

static int buffer_append_int(Buffer *buffer, int value)
{
    char number[32];
    int length = sqlite3_snprintf((int)sizeof(number), number, "%d", value) == NULL
        ? -1
        : (int)strlen(number);
    return length >= 0 && buffer_append(buffer, number, (size_t)length);
}

static char *copy_bytes(const char *source, size_t length)
{
    char *copy = (char *)sqlite3_malloc64((sqlite3_uint64)length + 1);
    if (copy == NULL) {
        return NULL;
    }
    if (length != 0) {
        memcpy(copy, source, length);
    }
    copy[length] = '\0';
    return copy;
}

static void free_archive(Archive *archive)
{
    int i;
    if (archive->revisions != NULL) {
        for (i = 0; i < archive->count; ++i) {
            sqlite3_free(archive->revisions[i].date);
            sqlite3_free(archive->revisions[i].author);
            sqlite3_free(archive->revisions[i].state);
            sqlite3_free(archive->revisions[i].branches);
            sqlite3_free(archive->revisions[i].log);
            sqlite3_free(archive->revisions[i].text);
        }
        sqlite3_free(archive->revisions);
    }
    if (archive->symbols != NULL) {
        for (i = 0; i < archive->symbol_count; ++i) {
            sqlite3_free(archive->symbols[i].name);
            sqlite3_free(archive->symbols[i].revision);
        }
        sqlite3_free(archive->symbols);
    }
    if (archive->branches != NULL) {
        for (i = 0; i < archive->branch_count; ++i) {
            int j;
            sqlite3_free(archive->branches[i].number);
            sqlite3_free(archive->branches[i].parent_revision);
            for (j = 0; j < archive->branches[i].count; ++j) {
                Revision *revision = &archive->branches[i].revisions[j];
                sqlite3_free(revision->date);
                sqlite3_free(revision->author);
                sqlite3_free(revision->state);
                sqlite3_free(revision->branches);
                sqlite3_free(revision->log);
                sqlite3_free(revision->text);
            }
            sqlite3_free(archive->branches[i].revisions);
        }
        sqlite3_free(archive->branches);
    }
    archive->revisions = NULL;
    archive->symbols = NULL;
    archive->branches = NULL;
    archive->count = 0;
    archive->symbol_count = 0;
    archive->symbol_capacity = 0;
    archive->branch_count = 0;
    archive->branch_capacity = 0;
}

static int read_line(const char *source, size_t length, size_t *position,
                     const char **line, size_t *line_length)
{
    size_t end = *position;
    while (end < length && source[end] != '\n') {
        ++end;
    }
    if (end == length) {
        return 0;
    }
    *line = source + *position;
    *line_length = end - *position;
    *position = end + 1;
    return 1;
}

static int is_horizontal_space(char character)
{
    return character == ' ' || character == '\t';
}

static int line_matches(const char *line, size_t line_length, const char *expected)
{
    size_t line_position = 0;
    size_t expected_position = 0;
    size_t expected_length = strlen(expected);
    while (line_position < line_length &&
           expected_position < expected_length) {
        if (is_horizontal_space(line[line_position]) &&
            is_horizontal_space(expected[expected_position])) {
            while (line_position < line_length &&
                   is_horizontal_space(line[line_position])) {
                ++line_position;
            }
            while (expected_position < expected_length &&
                   is_horizontal_space(expected[expected_position])) {
                ++expected_position;
            }
        } else if (line[line_position++] != expected[expected_position++]) {
            return 0;
        }
    }
    return line_position == line_length &&
           expected_position == expected_length;
}

static int skip_empty_lines(const char *source, size_t length,
                            size_t *position)
{
    size_t start;
    const char *line;
    size_t line_length;
    int skipped = 0;
    while (*position < length) {
        start = *position;
        if (!read_line(source, length, position, &line, &line_length)) {
            return -1;
        }
        if (line_length != 0) {
            *position = start;
            break;
        }
        ++skipped;
    }
    return skipped;
}

static int parse_revision_line(const char *line, size_t line_length, int expected)
{
    char expected_line[32];
    sqlite3_snprintf((int)sizeof(expected_line), expected_line, "1.%d", expected);
    return line_matches(line, line_length, expected_line);
}

static int parse_decimal(const char *text, size_t length, int *value)
{
    size_t i;
    int parsed = 0;
    if (length == 0) {
        return 0;
    }
    for (i = 0; i < length; ++i) {
        int digit;
        if (text[i] < '0' || text[i] > '9') {
            return 0;
        }
        digit = text[i] - '0';
        if (parsed > (INT_MAX - digit) / 10) {
            return 0;
        }
        parsed = parsed * 10 + digit;
    }
    *value = parsed;
    return 1;
}

static int parse_trunk_revision_text(const char *text, size_t length,
                                     int *revision)
{
    if (length < 3 || length > 16 ||
        text[0] != '1' || text[1] != '.' ||
        !parse_decimal((const char *)text + 2, (size_t)length - 2,
                       revision)) {
        return 0;
    }
    return *revision >= 1 && *revision <= RCS_MAX_REVISIONS;
}

static int parse_head_line(const char *line, size_t line_length, int *head)
{
    size_t position = 0;
    size_t revision_start;
    size_t revision_end;
    while (position < line_length && is_horizontal_space(line[position])) {
        ++position;
    }
    if (line_length - position < 4 ||
        memcmp(line + position, "head", 4) != 0) {
        return 0;
    }
    position += 4;
    if (position == line_length ||
        !is_horizontal_space(line[position])) {
        return 0;
    }
    while (position < line_length && is_horizontal_space(line[position])) {
        ++position;
    }
    revision_start = position;
    while (position < line_length && line[position] != ';' &&
           !is_horizontal_space(line[position])) {
        ++position;
    }
    revision_end = position;
    while (position < line_length && is_horizontal_space(line[position])) {
        ++position;
    }
    return position + 1 == line_length && line[position] == ';' &&
           parse_trunk_revision_text(line + revision_start,
                                     revision_end - revision_start, head);
}

static int parse_number_components(const char *text, size_t length,
                                   int *components, int *component_count)
{
    size_t start = 0;
    int count = 0;
    while (start < length) {
        size_t end = start;
        if (count == RCS_MAX_REVISION_COMPONENTS) {
            return 0;
        }
        while (end < length && text[end] != '.') {
            ++end;
        }
        if ((end - start > 1 && text[start] == '0') ||
            !parse_decimal(text + start, end - start, &components[count])) {
            return 0;
        }
        ++count;
        if (end == length) {
            *component_count = count;
            return 1;
        }
        start = end + 1;
        if (start == length) {
            return 0;
        }
    }
    return 0;
}

static int parse_branch_path(const char *text, size_t length,
                             char **parent_revision, int *base_revision,
                             int *branch_id)
{
    int parts[RCS_MAX_REVISION_COMPONENTS];
    int count;
    Buffer parent = {NULL, 0, 0};
    int i;
    if (!parse_number_components(text, length, parts, &count) ||
        count < 3 || count % 2 == 0 || parts[0] != 1) {
        return 0;
    }
    for (i = 1; i < count; ++i) {
        if (parts[i] < 1 || parts[i] > RCS_MAX_REVISIONS) {
            return 0;
        }
    }
    for (i = 0; i < count - 1; ++i) {
        if ((i != 0 && !buffer_append(&parent, ".", 1)) ||
            !buffer_append_int(&parent, parts[i])) {
            sqlite3_free(parent.data);
            return 0;
        }
    }
    if (parent_revision != NULL) {
        *parent_revision = parent.data;
    } else {
        sqlite3_free(parent.data);
    }
    *base_revision = parts[1];
    *branch_id = parts[count - 1];
    return 1;
}

static int parse_branch_revision_path(const char *text, size_t length,
                                     char **branch_number,
                                     int *base_revision, int *branch_id,
                                     int *branch_revision)
{
    int parts[RCS_MAX_REVISION_COMPONENTS];
    int count;
    size_t branch_length = length;
    if (!parse_number_components(text, length, parts, &count) ||
        count < 4 || count % 2 != 0 || parts[0] != 1) {
        return 0;
    }
    while (branch_length > 0 && text[branch_length - 1] != '.') {
        --branch_length;
    }
    if (branch_length < 2) {
        return 0;
    }
    --branch_length;
    if (branch_length < 3) {
        return 0;
    }
    if (!parse_branch_path(text, branch_length, NULL, base_revision,
                           branch_id)) {
        return 0;
    }
    if (parts[count - 1] < 1 ||
        parts[count - 1] > RCS_MAX_REVISIONS) {
        return 0;
    }
    *branch_number = copy_bytes(text, branch_length);
    if (*branch_number == NULL) {
        return 0;
    }
    *branch_revision = parts[count - 1];
    return 1;
}

static char *make_child_branch_number(const char *parent_revision,
                                     int branch_id)
{
    Buffer number = {NULL, 0, 0};
    if (!buffer_append_cstr(&number, parent_revision) ||
        !buffer_append(&number, ".", 1) ||
        !buffer_append_int(&number, branch_id)) {
        sqlite3_free(number.data);
        return NULL;
    }
    return number.data;
}

static char *make_branch_revision_path(const char *branch_number,
                                      int revision)
{
    Buffer number = {NULL, 0, 0};
    if (!buffer_append_cstr(&number, branch_number) ||
        !buffer_append(&number, ".", 1) ||
        !buffer_append_int(&number, revision)) {
        sqlite3_free(number.data);
        return NULL;
    }
    return number.data;
}

static Branch *find_branch(Archive *archive, const char *number)
{
    int i;
    for (i = 0; i < archive->branch_count; ++i) {
        if (strcmp(archive->branches[i].number, number) == 0) {
            return &archive->branches[i];
        }
    }
    return NULL;
}

static Branch *add_branch(Archive *archive, const char *number,
                          const char *parent_revision, int base_revision,
                          int branch_id)
{
    Branch *branches;
    Branch branch;
    int capacity;
    int position = 0;
    if (archive->branch_count >= RCS_MAX_REVISIONS) {
        return NULL;
    }
    if (archive->branch_count == archive->branch_capacity) {
        capacity = archive->branch_capacity == 0 ? 8 :
                   archive->branch_capacity * 2;
        if (capacity > RCS_MAX_REVISIONS) {
            capacity = RCS_MAX_REVISIONS;
        }
        branches = (Branch *)sqlite3_realloc64(
            archive->branches, (sqlite3_uint64)capacity * sizeof(Branch));
        if (branches == NULL) {
            return NULL;
        }
        archive->branches = branches;
        archive->branch_capacity = capacity;
    }
    memset(&branch, 0, sizeof(branch));
    branch.number = copy_bytes(number, strlen(number));
    branch.parent_revision = copy_bytes(parent_revision,
                                        strlen(parent_revision));
    if (branch.number == NULL || branch.parent_revision == NULL) {
        sqlite3_free(branch.number);
        sqlite3_free(branch.parent_revision);
        return NULL;
    }
    branch.base_revision = base_revision;
    branch.branch_id = branch_id;
    while (position < archive->branch_count &&
           strcmp(archive->branches[position].number, number) < 0) {
        ++position;
    }
    memmove(&archive->branches[position + 1], &archive->branches[position],
            (size_t)(archive->branch_count - position) * sizeof(Branch));
    archive->branches[position] = branch;
    ++archive->branch_count;
    return &archive->branches[position];
}

static int reserve_branch_revisions(Branch *branch, int needed)
{
    Revision *revisions;
    int capacity;
    if (needed <= branch->capacity) {
        return 1;
    }
    capacity = branch->capacity == 0 ? 4 : branch->capacity;
    while (capacity < needed) {
        if (capacity > RCS_MAX_REVISIONS / 2) {
            capacity = RCS_MAX_REVISIONS;
            break;
        }
        capacity *= 2;
    }
    if (capacity < needed) {
        return 0;
    }
    revisions = (Revision *)sqlite3_realloc64(
        branch->revisions, (sqlite3_uint64)capacity * sizeof(Revision));
    if (revisions == NULL) {
        return 0;
    }
    memset(revisions + branch->capacity, 0,
           (size_t)(capacity - branch->capacity) * sizeof(Revision));
    branch->revisions = revisions;
    branch->capacity = capacity;
    return 1;
}

static int valid_token(const char *text, size_t length)
{
    size_t i;
    if (length == 0) {
        return 0;
    }
    for (i = 0; i < length; ++i) {
        unsigned char character = (unsigned char)text[i];
        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') ||
              character == '_' || character == '-' || character == '.')) {
            return 0;
        }
    }
    return 1;
}

static int valid_symbol_name(const char *name, size_t length)
{
    size_t i;
    if (length == 0 || length > RCS_MAX_SYMBOL_NAME ||
        !((name[0] >= 'a' && name[0] <= 'z') ||
          (name[0] >= 'A' && name[0] <= 'Z') || name[0] == '_')) {
        return 0;
    }
    for (i = 1; i < length; ++i) {
        unsigned char character = (unsigned char)name[i];
        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') ||
              character == '_' || character == '-' || character == '.')) {
            return 0;
        }
    }
    return 1;
}

static int reserve_symbols(Archive *archive, int needed)
{
    int capacity;
    Symbol *symbols;
    if (needed <= archive->symbol_capacity) {
        return 1;
    }
    capacity = archive->symbol_capacity == 0 ? 8 : archive->symbol_capacity;
    while (capacity < needed) {
        if (capacity > RCS_MAX_REVISIONS / 2) {
            capacity = RCS_MAX_REVISIONS;
            break;
        }
        capacity *= 2;
    }
    symbols = (Symbol *)sqlite3_realloc64(
        archive->symbols, (sqlite3_uint64)capacity * sizeof(Symbol));
    if (symbols == NULL) {
        return 0;
    }
    archive->symbols = symbols;
    archive->symbol_capacity = capacity;
    return 1;
}

static int resolve_revision(const Archive *archive, sqlite3_value *value,
                            int *revision)
{
    const unsigned char *text = sqlite3_value_text(value);
    int length = sqlite3_value_bytes(value);
    int i;
    if (text == NULL) {
        return 0;
    }
    if (parse_trunk_revision_text((const char *)text, (size_t)length,
                                  revision)) {
        return *revision <= archive->count;
    }
    if (!valid_symbol_name((const char *)text, (size_t)length)) {
        return 0;
    }
    for (i = 0; i < archive->symbol_count; ++i) {
        if (strlen(archive->symbols[i].name) == (size_t)length &&
            memcmp(archive->symbols[i].name, text, (size_t)length) == 0) {
            if (archive->symbols[i].is_branch) {
                return 0;
            }
            return parse_trunk_revision_text(
                archive->symbols[i].revision,
                strlen(archive->symbols[i].revision), revision);
        }
    }
    return 0;
}

static int is_utf8_continuation(const char *text, size_t position)
{
    unsigned char character = (unsigned char)text[position];
    return (character & 0xc0) == 0x80;
}

static int valid_utf8(const char *text, size_t length)
{
    size_t i = 0;
    while (i < length) {
        unsigned char first = (unsigned char)text[i];
        size_t remaining = length - i;
        if (first <= 0x7f) {
            ++i;
        } else if (first >= 0xc2 && first <= 0xdf) {
            if (remaining < 2 || !is_utf8_continuation(text, i + 1)) {
                return 0;
            }
            i += 2;
        } else if (first == 0xe0) {
            if (remaining < 3 || (unsigned char)text[i + 1] < 0xa0 ||
                (unsigned char)text[i + 1] > 0xbf ||
                !is_utf8_continuation(text, i + 2)) {
                return 0;
            }
            i += 3;
        } else if ((first >= 0xe1 && first <= 0xec) ||
                   (first >= 0xee && first <= 0xef)) {
            if (remaining < 3 || !is_utf8_continuation(text, i + 1) ||
                !is_utf8_continuation(text, i + 2)) {
                return 0;
            }
            i += 3;
        } else if (first == 0xed) {
            if (remaining < 3 || (unsigned char)text[i + 1] < 0x80 ||
                (unsigned char)text[i + 1] > 0x9f ||
                !is_utf8_continuation(text, i + 2)) {
                return 0;
            }
            i += 3;
        } else if (first == 0xf0) {
            if (remaining < 4 || (unsigned char)text[i + 1] < 0x90 ||
                (unsigned char)text[i + 1] > 0xbf ||
                !is_utf8_continuation(text, i + 2) ||
                !is_utf8_continuation(text, i + 3)) {
                return 0;
            }
            i += 4;
        } else if (first >= 0xf1 && first <= 0xf3) {
            if (remaining < 4 || !is_utf8_continuation(text, i + 1) ||
                !is_utf8_continuation(text, i + 2) ||
                !is_utf8_continuation(text, i + 3)) {
                return 0;
            }
            i += 4;
        } else if (first == 0xf4) {
            if (remaining < 4 || (unsigned char)text[i + 1] < 0x80 ||
                (unsigned char)text[i + 1] > 0x8f ||
                !is_utf8_continuation(text, i + 2) ||
                !is_utf8_continuation(text, i + 3)) {
                return 0;
            }
            i += 4;
        } else {
            return 0;
        }
    }
    return 1;
}

static char *normalize_line_whitespace(const char *line, size_t line_length,
                                       size_t *normalized_length)
{
    char *normalized = copy_bytes(line, line_length);
    size_t read_position;
    size_t write_position = 0;
    int pending_space = 0;
    if (normalized == NULL) {
        return NULL;
    }
    for (read_position = 0; read_position < line_length; ++read_position) {
        if (is_horizontal_space(normalized[read_position])) {
            pending_space = write_position != 0;
        } else {
            if (pending_space) {
                normalized[write_position++] = ' ';
                pending_space = 0;
            }
            normalized[write_position++] = normalized[read_position];
        }
    }
    normalized[write_position] = '\0';
    *normalized_length = write_position;
    return normalized;
}

static int parse_metadata_line_normalized(const char *line,
                                          size_t line_length,
                                          Revision *revision)
{
    char *copy;
    char *date_field;
    char *author_field;
    char *state_field;
    size_t date_length;
    size_t author_length;
    size_t state_length;

    copy = copy_bytes(line, line_length);
    if (copy == NULL) {
        return 0;
    }
    date_field = strstr(copy, "date ");
    author_field = strstr(copy, "; author ");
    state_field = strstr(copy, "; state ");
    if (date_field != copy || author_field == NULL || state_field == NULL ||
        author_field < date_field + 5 || state_field < author_field + 9 ||
        line_length == 0 || copy[line_length - 1] != ';') {
        sqlite3_free(copy);
        return 0;
    }
    date_field += 5;
    date_length = (size_t)(author_field - date_field);
    author_field += 9;
    author_length = (size_t)(state_field - author_field);
    state_field += 8;
    state_length = (size_t)(copy + line_length - state_field);
    if (date_length == 0 || author_length == 0 || state_length < 2) {
        sqlite3_free(copy);
        return 0;
    }
    revision->date = copy_bytes(date_field, date_length);
    revision->author = copy_bytes(author_field, author_length);
    revision->state = copy_bytes(state_field, state_length - 1);
    sqlite3_free(copy);
    return revision->date != NULL && revision->author != NULL &&
           revision->state != NULL &&
           valid_token(revision->date, date_length) &&
           valid_token(revision->author, author_length) &&
           valid_token(revision->state, state_length - 1);
}

static int parse_metadata_line(const char *line, size_t line_length,
                               Revision *revision)
{
    char *normalized;
    size_t normalized_length;
    int valid;
    normalized = normalize_line_whitespace(line, line_length,
                                           &normalized_length);
    if (normalized == NULL) {
        return 0;
    }
    valid = parse_metadata_line_normalized(normalized, normalized_length,
                                           revision);
    sqlite3_free(normalized);
    return valid;
}

static int parse_quoted(const char *source, size_t length, size_t *position,
                        char **value, size_t *value_length)
{
    Buffer decoded;
    size_t i;
    int closed = 0;

    decoded.data = NULL;
    decoded.length = 0;
    decoded.capacity = 0;
    if (*position >= length || source[*position] != '@') {
        return 0;
    }
    i = *position + 1;
    while (i < length) {
        if (source[i] != '@') {
            if (!buffer_append(&decoded, source + i, 1)) {
                sqlite3_free(decoded.data);
                return 0;
            }
            ++i;
        } else if (i + 1 < length && source[i + 1] == '@') {
            if (!buffer_append(&decoded, "@", 1)) {
                sqlite3_free(decoded.data);
                return 0;
            }
            i += 2;
        } else {
            ++i;
            closed = 1;
            break;
        }
    }
    if (!closed) {
        sqlite3_free(decoded.data);
        return 0;
    }
    if (decoded.data == NULL) {
        decoded.data = copy_bytes("", 0);
        if (decoded.data == NULL) {
            return 0;
        }
    }
    *position = i;
    *value = decoded.data;
    *value_length = decoded.length;
    return 1;
}

static int expect_line(const char *source, size_t length, size_t *position,
                       const char *expected)
{
    const char *line;
    size_t line_length;
    return read_line(source, length, position, &line, &line_length) &&
           line_matches(line, line_length, expected);
}

static int append_symbol(Archive *archive, const char *name, size_t name_length,
                         const char *revision, int is_branch)
{
    char *copy;
    char *revision_copy;
    if (archive->symbol_count >= RCS_MAX_REVISIONS ||
        !reserve_symbols(archive, archive->symbol_count + 1)) {
        return 0;
    }
    copy = copy_bytes(name, name_length);
    revision_copy = copy_bytes(revision, strlen(revision));
    if (copy == NULL || revision_copy == NULL) {
        sqlite3_free(copy);
        sqlite3_free(revision_copy);
        return 0;
    }
    archive->symbols[archive->symbol_count].name = copy;
    archive->symbols[archive->symbol_count].revision = revision_copy;
    archive->symbols[archive->symbol_count].is_branch = is_branch;
    ++archive->symbol_count;
    return 1;
}

static int parse_symbols_line_normalized(const char *line,
                                         size_t line_length,
                                         Archive *archive, int head)
{
    size_t position = 8;
    size_t end = line_length;
    if (line_matches(line, line_length, "symbols;")) {
        return 1;
    }
    if (line_length < 10 || memcmp(line, "symbols ", 8) != 0 ||
        line[line_length - 1] != ';') {
        return 0;
    }
    --end;
    while (position < end) {
        size_t name_start;
        size_t name_length;
        size_t revision_start;
        int revision = 0;
        int base_revision = 0;
        int branch_id = 0;
        int is_branch = 0;
        char *parent_revision = NULL;
        char *revision_text;
        int i;
        while (position < end && line[position] == ' ') {
            ++position;
        }
        name_start = position;
        while (position < end && line[position] != ':' &&
               line[position] != ' ') {
            ++position;
        }
        if (position == name_start || position >= end ||
            line[position] != ':' ||
            !valid_symbol_name(line + name_start, position - name_start)) {
            return 0;
        }
        name_length = position - name_start;
        for (i = 0; i < archive->symbol_count; ++i) {
            if (strlen(archive->symbols[i].name) == name_length &&
                memcmp(archive->symbols[i].name, line + name_start,
                       name_length) == 0) {
                return 0;
            }
        }
        ++position;
        while (position < end && line[position] == ' ') {
            ++position;
        }
        revision_start = position;
        while (position < end && line[position] != ' ') {
            ++position;
        }
        if (parse_trunk_revision_text(line + revision_start,
                                      position - revision_start, &revision)) {
            if (revision > head) {
                return 0;
            }
        } else if (parse_branch_path(line + revision_start,
                                     position - revision_start,
                                     &parent_revision, &base_revision,
                                     &branch_id) &&
                   base_revision <= head) {
            is_branch = 1;
        } else {
            sqlite3_free(parent_revision);
            return 0;
        }
        revision_text = copy_bytes(line + revision_start,
                                   position - revision_start);
        if (revision_text == NULL ||
            !append_symbol(archive, line + name_start, name_length,
                           revision_text, is_branch)) {
            sqlite3_free(revision_text);
            sqlite3_free(parent_revision);
            return 0;
        }
        if (is_branch &&
            find_branch(archive, revision_text) == NULL &&
            add_branch(archive, revision_text, parent_revision,
                       base_revision, branch_id) == NULL) {
            sqlite3_free(revision_text);
            sqlite3_free(parent_revision);
            return 0;
        }
        sqlite3_free(revision_text);
        sqlite3_free(parent_revision);
        while (position < end && line[position] == ' ') {
            ++position;
        }
    }
    if (position != end || archive->symbol_count == 0) {
        return 0;
    }
    {
        int i;
        for (i = 1; i < archive->symbol_count; ++i) {
            Symbol current = archive->symbols[i];
            int j = i;
            while (j > 0 &&
                   strcmp(archive->symbols[j - 1].name, current.name) > 0) {
                archive->symbols[j] = archive->symbols[j - 1];
                --j;
            }
            archive->symbols[j] = current;
        }
    }
    return 1;
}

static int parse_symbols_line(const char *line, size_t line_length,
                              Archive *archive, int head)
{
    char *normalized;
    size_t normalized_length;
    int valid;
    normalized = normalize_line_whitespace(line, line_length,
                                           &normalized_length);
    if (normalized == NULL) {
        return 0;
    }
    valid = parse_symbols_line_normalized(normalized, normalized_length,
                                          archive, head);
    sqlite3_free(normalized);
    return valid;
}

static int parse_symbols_header(const char *source, size_t length,
                                size_t *position, Archive *archive,
                                int head)
{
    const char *line;
    size_t line_length;
    Buffer symbols = {NULL, 0, 0};
    int complete = 0;
    int valid;
    if (!read_line(source, length, position, &line, &line_length) ||
        !buffer_append(&symbols, line, line_length)) {
        sqlite3_free(symbols.data);
        return 0;
    }
    complete = memchr(line, ';', line_length) != NULL;
    while (!complete) {
        if (!read_line(source, length, position, &line, &line_length) ||
            !buffer_append(&symbols, " ", 1) ||
            !buffer_append(&symbols, line, line_length) ||
            symbols.length > RCS_MAX_ARCHIVE) {
            sqlite3_free(symbols.data);
            return 0;
        }
        complete = memchr(line, ';', line_length) != NULL;
    }
    valid = parse_symbols_line(symbols.data, symbols.length, archive, head);
    sqlite3_free(symbols.data);
    return valid;
}

static int parse_branches_line_normalized(const char *line,
                                          size_t line_length,
                                          Revision *revision)
{
    size_t position;
    size_t end;
    char *references;
    if (line_matches(line, line_length, "branches;")) {
        return 1;
    }
    if (line_length < 11 || memcmp(line, "branches ", 9) != 0 ||
        line[line_length - 1] != ';') {
        return 0;
    }
    position = 9;
    end = line_length - 1;
    while (position < end) {
        size_t start;
        char *branch_number = NULL;
        int base_revision;
        int branch_id;
        int branch_revision;
        while (position < end && line[position] == ' ') {
            ++position;
        }
        start = position;
        while (position < end && line[position] != ' ') {
            ++position;
        }
        if (!parse_branch_revision_path(line + start, position - start,
                                        &branch_number, &base_revision,
                                        &branch_id, &branch_revision) ||
            branch_revision != 1) {
            sqlite3_free(branch_number);
            return 0;
        }
        sqlite3_free(branch_number);
    }
    references = copy_bytes(line + 9, end - 9);
    if (references == NULL) {
        return 0;
    }
    revision->branches = references;
    return 1;
}

static int parse_branches_line(const char *line, size_t line_length,
                               Revision *revision)
{
    char *normalized;
    size_t normalized_length;
    int valid;
    normalized = normalize_line_whitespace(line, line_length,
                                           &normalized_length);
    if (normalized == NULL) {
        return 0;
    }
    valid = parse_branches_line_normalized(normalized, normalized_length,
                                           revision);
    sqlite3_free(normalized);
    return valid;
}

static int parse_branches_header(const char *source, size_t length,
                                 size_t *position, Revision *revision)
{
    const char *line;
    size_t line_length;
    Buffer branches = {NULL, 0, 0};
    int complete = 0;
    int valid;
    if (!read_line(source, length, position, &line, &line_length) ||
        !buffer_append(&branches, line, line_length)) {
        sqlite3_free(branches.data);
        return 0;
    }
    complete = memchr(line, ';', line_length) != NULL;
    while (!complete) {
        if (!read_line(source, length, position, &line, &line_length) ||
            !buffer_append(&branches, " ", 1) ||
            !buffer_append(&branches, line, line_length) ||
            branches.length > RCS_MAX_ARCHIVE) {
            sqlite3_free(branches.data);
            return 0;
        }
        complete = memchr(line, ';', line_length) != NULL;
    }
    valid = parse_branches_line(branches.data, branches.length, revision);
    sqlite3_free(branches.data);
    return valid;
}

static int read_wrapped_header(const char *source, size_t length,
                              size_t *position, Buffer *field)
{
    const char *line;
    size_t line_length;
    int complete;
    memset(field, 0, sizeof(*field));
    if (!read_line(source, length, position, &line, &line_length) ||
        !buffer_append(field, line, line_length)) {
        sqlite3_free(field->data);
        memset(field, 0, sizeof(*field));
        return 0;
    }
    complete = memchr(line, ';', line_length) != NULL;
    while (!complete) {
        if (!read_line(source, length, position, &line, &line_length) ||
            !buffer_append(field, " ", 1) ||
            !buffer_append(field, line, line_length) ||
            field->length > RCS_MAX_ARCHIVE) {
            sqlite3_free(field->data);
            memset(field, 0, sizeof(*field));
            return 0;
        }
        complete = memchr(line, ';', line_length) != NULL;
    }
    return 1;
}

static int header_value_bounds(const char *line, size_t line_length,
                               const char *key, size_t *value_start,
                               size_t *value_end, size_t *trailing_start)
{
    size_t position = 0;
    size_t key_length = strlen(key);
    while (position < line_length && is_horizontal_space(line[position])) {
        ++position;
    }
    if (line_length - position < key_length ||
        memcmp(line + position, key, key_length) != 0) {
        return 0;
    }
    position += key_length;
    if (position < line_length && line[position] != ';' &&
        !is_horizontal_space(line[position])) {
        return 0;
    }
    while (position < line_length && is_horizontal_space(line[position])) {
        ++position;
    }
    *value_start = position;
    while (position < line_length && line[position] != ';') {
        ++position;
    }
    if (position == line_length) {
        return 0;
    }
    *value_end = position;
    ++position;
    while (position < line_length && is_horizontal_space(line[position])) {
        ++position;
    }
    *trailing_start = position;
    return 1;
}

static int valid_admin_tokens(const char *text, size_t length,
                              int is_lock_list)
{
    size_t position = 0;
    while (position < length) {
        size_t start;
        size_t end;
        while (position < length && is_horizontal_space(text[position])) {
            ++position;
        }
        if (position == length) {
            break;
        }
        start = position;
        while (position < length && !is_horizontal_space(text[position])) {
            ++position;
        }
        end = position;
        if (is_lock_list) {
            size_t colon = start;
            int revision;
            int base_revision;
            int branch_id;
            int branch_revision;
            char *branch_number = NULL;
            while (colon < end && text[colon] != ':') {
                ++colon;
            }
            if (colon == start || colon == end ||
                !valid_token(text + start, colon - start) ||
                (!parse_trunk_revision_text(text + colon + 1,
                                            end - colon - 1, &revision) &&
                 !parse_branch_revision_path(text + colon + 1,
                                             end - colon - 1,
                                             &branch_number, &base_revision,
                                             &branch_id,
                                             &branch_revision))) {
                sqlite3_free(branch_number);
                return 0;
            }
            sqlite3_free(branch_number);
        } else if (!valid_token(text + start, end - start)) {
            return 0;
        }
    }
    return 1;
}

static int parse_access_header(const char *source, size_t length,
                               size_t *position)
{
    Buffer field;
    char *normalized;
    size_t normalized_length;
    size_t value_start;
    size_t value_end;
    size_t trailing_start;
    int valid;
    if (!read_wrapped_header(source, length, position, &field)) {
        return 0;
    }
    normalized = normalize_line_whitespace(field.data, field.length,
                                           &normalized_length);
    sqlite3_free(field.data);
    if (normalized == NULL) {
        return 0;
    }
    valid = header_value_bounds(normalized, normalized_length, "access",
                                &value_start, &value_end, &trailing_start) &&
            trailing_start == normalized_length &&
            valid_admin_tokens(normalized + value_start,
                               value_end - value_start, 0);
    sqlite3_free(normalized);
    return valid;
}

static int parse_locks_header(const char *source, size_t length,
                              size_t *position)
{
    Buffer field;
    char *normalized;
    size_t normalized_length;
    size_t value_start;
    size_t value_end;
    size_t trailing_start;
    int valid;
    if (!read_wrapped_header(source, length, position, &field)) {
        return 0;
    }
    normalized = normalize_line_whitespace(field.data, field.length,
                                           &normalized_length);
    sqlite3_free(field.data);
    if (normalized == NULL) {
        return 0;
    }
    valid = header_value_bounds(normalized, normalized_length, "locks",
                                &value_start, &value_end, &trailing_start) &&
            valid_admin_tokens(normalized + value_start,
                               value_end - value_start, 1);
    if (valid && trailing_start < normalized_length) {
        valid = line_matches(normalized + trailing_start,
                             normalized_length - trailing_start, "strict;");
    } else if (valid) {
        size_t strict_start = *position;
        const char *line;
        size_t line_length;
        if (read_line(source, length, position, &line, &line_length)) {
            if (!line_matches(line, line_length, "strict;")) {
                *position = strict_start;
            }
        }
    }
    sqlite3_free(normalized);
    return valid;
}

static int parse_quoted_header(const char *source, size_t length,
                               size_t *position, const char *key,
                               int required)
{
    const char *line;
    size_t line_length;
    size_t key_position = 0;
    size_t key_length = strlen(key);
    char *value = NULL;
    size_t value_length;
    int valid;
    if (!read_line(source, length, position, &line, &line_length)) {
        return 0;
    }
    while (key_position < line_length &&
           is_horizontal_space(line[key_position])) {
        ++key_position;
    }
    if (key_position == line_length ||
        line_length - key_position < key_length ||
        memcmp(line + key_position, key, key_length) != 0) {
        if (required) {
            return 0;
        }
        *position -= line_length + 1;
        return 1;
    }
    key_position += key_length;
    if (key_position < line_length &&
        !is_horizontal_space(line[key_position])) {
        return 0;
    }
    while (key_position < line_length &&
           is_horizontal_space(line[key_position])) {
        ++key_position;
    }
    valid = parse_quoted(line, line_length, &key_position, &value,
                         &value_length);
    sqlite3_free(value);
    if (!valid) {
        return 0;
    }
    while (key_position < line_length &&
           is_horizontal_space(line[key_position])) {
        ++key_position;
    }
    return key_position + 1 == line_length && line[key_position] == ';';
}

static int parse_next_line(const char *line, size_t line_length,
                           const char **revision, size_t *revision_length)
{
    size_t position = 0;
    size_t start;
    while (position < line_length && is_horizontal_space(line[position])) {
        ++position;
    }
    if (line_length - position < 4 ||
        memcmp(line + position, "next", 4) != 0) {
        return 0;
    }
    position += 4;
    if (position < line_length && line[position] != ';' &&
        !is_horizontal_space(line[position])) {
        return 0;
    }
    while (position < line_length && is_horizontal_space(line[position])) {
        ++position;
    }
    start = position;
    while (position < line_length && line[position] != ';' &&
           !is_horizontal_space(line[position])) {
        ++position;
    }
    *revision = line + start;
    *revision_length = position - start;
    while (position < line_length && is_horizontal_space(line[position])) {
        ++position;
    }
    if (position == line_length || line[position++] != ';') {
        return 0;
    }
    while (position < line_length && is_horizontal_space(line[position])) {
        ++position;
    }
    return position == line_length;
}

static Revision *find_archive_revision(Archive *archive, const char *number,
                                       size_t number_length)
{
    int revision;
    int base_revision;
    int branch_id;
    char *branch_number = NULL;
    int branch_revision;
    Branch *branch;
    if (parse_trunk_revision_text(number, number_length, &revision)) {
        return revision <= archive->count
            ? &archive->revisions[revision - 1] : NULL;
    }
    if (!parse_branch_revision_path(number, number_length, &branch_number,
                                    &base_revision, &branch_id,
                                    &branch_revision)) {
        return NULL;
    }
    branch = find_branch(archive, branch_number);
    sqlite3_free(branch_number);
    return branch != NULL && branch_revision <= branch->count
        ? &branch->revisions[branch_revision - 1] : NULL;
}

static int validate_branch_references(Archive *archive,
                                      const char *parent_revision,
                                      const char *references)
{
    size_t position = 0;
    while (references != NULL && references[position] != '\0') {
        size_t end = position;
        char *branch_number = NULL;
        int base_revision;
        int branch_id;
        int branch_revision;
        Branch *branch;
        while (references[end] != '\0' && references[end] != ' ') {
            ++end;
        }
        if (!parse_branch_revision_path(references + position, end - position,
                                        &branch_number, &base_revision,
                                        &branch_id, &branch_revision) ||
            branch_revision != 1) {
            sqlite3_free(branch_number);
            return 0;
        }
        branch = find_branch(archive, branch_number);
        if (branch == NULL || branch->count == 0 ||
            strcmp(branch->parent_revision, parent_revision) != 0) {
            sqlite3_free(branch_number);
            return 0;
        }
        sqlite3_free(branch_number);
        position = end;
        while (references[position] == ' ') {
            ++position;
        }
    }
    return 1;
}

static int branch_parent_links_to_child(Archive *archive,
                                        const Branch *branch)
{
    Revision *parent = find_archive_revision(
        archive, branch->parent_revision, strlen(branch->parent_revision));
    char *first_revision;
    const char *references;
    size_t position = 0;
    int found = 0;
    if (parent == NULL || parent->branches == NULL) {
        return 0;
    }
    first_revision = make_branch_revision_path(branch->number, 1);
    if (first_revision == NULL) {
        return 0;
    }
    references = parent->branches;
    while (references[position] != '\0') {
        size_t end = position;
        while (references[end] != '\0' && references[end] != ' ') {
            ++end;
        }
        if (strlen(first_revision) == end - position &&
            memcmp(references + position, first_revision,
                   end - position) == 0) {
            found = 1;
            break;
        }
        position = end;
        while (references[position] == ' ') {
            ++position;
        }
    }
    sqlite3_free(first_revision);
    return found;
}

static int validate_archive_branch_links(Archive *archive)
{
    int number;
    int i;
    for (number = 1; number <= archive->count; ++number) {
        Buffer parent_number = {NULL, 0, 0};
        Revision *parent = &archive->revisions[number - 1];
        int valid = buffer_append_cstr(&parent_number, "1.") &&
                    buffer_append_int(&parent_number, number) &&
                    validate_branch_references(
                        archive, parent_number.data, parent->branches);
        sqlite3_free(parent_number.data);
        if (!valid) {
            return 0;
        }
    }
    for (i = 0; i < archive->branch_count; ++i) {
        Branch *branch = &archive->branches[i];
        int revision_number;
        for (revision_number = 1; revision_number <= branch->count;
             ++revision_number) {
            Revision *parent = &branch->revisions[revision_number - 1];
            char *parent_number = make_branch_revision_path(
                branch->number, revision_number);
            int valid = parent_number != NULL &&
                validate_branch_references(archive, parent_number,
                                          parent->branches);
            sqlite3_free(parent_number);
            if (!valid) {
                return 0;
            }
        }
    }
    for (i = 0; i < archive->branch_count; ++i) {
        Branch *branch = &archive->branches[i];
        if (find_archive_revision(archive, branch->parent_revision,
                                  strlen(branch->parent_revision)) == NULL ||
            (branch->count > 0 &&
             !branch_parent_links_to_child(archive, branch))) {
            return 0;
        }
    }
    return 1;
}

static int normalize_branch_revisions(Archive *archive, Branch *branch);

static int parse_archive(const char *source, size_t length, Archive *archive)
{
    const char *line;
    const char *next_revision;
    char *description;
    size_t description_length;
    size_t line_length;
    size_t next_length;
    size_t position;
    size_t header_end;
    size_t i;
    size_t branch_records = 0;
    int head;
    int number;
    char expected[32];

    memset(archive, 0, sizeof(*archive));
    if (length == 0 || length > RCS_MAX_ARCHIVE ||
        memchr(source, '\0', length) != NULL) {
        return 0;
    }
    position = 0;
    if (!read_line(source, length, &position, &line, &line_length) ||
        !parse_head_line(line, line_length, &head) ||
        !parse_access_header(source, length, &position) ||
        !parse_symbols_header(source, length, &position, archive, head) ||
        !parse_locks_header(source, length, &position) ||
        !parse_quoted_header(source, length, &position, "comment", 1) ||
        !parse_quoted_header(source, length, &position, "expand", 0) ||
        skip_empty_lines(source, length, &position) < 1) {
        goto invalid;
    }
    header_end = position;
    archive->count = head;
    archive->revisions = (Revision *)sqlite3_malloc64(
        (sqlite3_uint64)head * sizeof(Revision));
    if (archive->revisions == NULL) {
        free_archive(archive);
        return 0;
    }
    memset(archive->revisions, 0, (size_t)head * sizeof(Revision));
    position = header_end;

    for (number = head; number >= 1; --number) {
        Revision *revision = &archive->revisions[number - 1];

        if (skip_empty_lines(source, length, &position) < 0 ||
            !read_line(source, length, &position, &line, &line_length) ||
            !parse_revision_line(line, line_length, number)) {
            goto invalid;
        }
        if (!read_line(source, length, &position, &line, &line_length)) {
            goto invalid;
        }
        if (!parse_metadata_line(line, line_length, revision)) {
            goto invalid;
        }
        if (!parse_branches_header(source, length, &position, revision) ||
        !read_line(source, length, &position, &line, &line_length) ||
        !parse_next_line(line, line_length, &next_revision,
                         &next_length)) {
            goto invalid;
        }
        if (number == 1) {
        if (next_length != 0) {
                goto invalid;
            }
        } else {
        sqlite3_snprintf((int)sizeof(expected), expected, "1.%d",
                         number - 1);
        if (next_length != strlen(expected) ||
            memcmp(next_revision, expected, next_length) != 0) {
                goto invalid;
            }
        }
    }

    while (position < length) {
        size_t record_start;
        int base_revision;
        int branch_id;
        int branch_revision;
        char *branch_number = NULL;
        char *parent_revision = NULL;
        Branch *branch;
        Revision *revision;
        char *next_branch_number = NULL;
        int next_base_revision;
        int next_branch_id;
        int next_branch_revision;

        if (skip_empty_lines(source, length, &position) < 0) {
            goto invalid;
        }
        record_start = position;
        if (!read_line(source, length, &position, &line, &line_length)) {
            goto invalid;
        }
        if (line_matches(line, line_length, "desc")) {
            position = record_start;
            break;
        }
        if (!parse_branch_revision_path(line, line_length, &branch_number,
                                        &base_revision, &branch_id,
                                        &branch_revision) ||
            base_revision > head ||
            !parse_branch_path(branch_number, strlen(branch_number),
                               &parent_revision, &base_revision,
                               &branch_id)) {
            sqlite3_free(branch_number);
            sqlite3_free(parent_revision);
            goto invalid;
        }
        branch = find_branch(archive, branch_number);
        if (branch == NULL) {
            branch = add_branch(archive, branch_number, parent_revision,
                                base_revision, branch_id);
        }
        sqlite3_free(branch_number);
        sqlite3_free(parent_revision);
        if (branch == NULL ||
            !reserve_branch_revisions(branch, branch_revision)) {
            goto invalid;
        }
        if (branch_revision > branch->count) {
            branch->count = branch_revision;
        }
        revision = &branch->revisions[branch_revision - 1];
        if (revision->date != NULL) {
            goto invalid;
        }
        if (!read_line(source, length, &position, &line, &line_length) ||
            !parse_metadata_line(line, line_length, revision) ||
            !parse_branches_header(source, length, &position, revision) ||
            !read_line(source, length, &position, &line, &line_length) ||
            !parse_next_line(line, line_length, &next_revision,
                             &next_length)) {
            goto invalid;
        }
        revision->next_seen = 1;
        if (next_length == 0) {
            revision->next_revision = 0;
        } else {
            if (!parse_branch_revision_path(next_revision, next_length,
                                            &next_branch_number,
                                            &next_base_revision,
                                            &next_branch_id,
                                            &next_branch_revision) ||
                strcmp(next_branch_number, branch->number) != 0 ||
                next_branch_revision != branch_revision + 1) {
                sqlite3_free(next_branch_number);
                goto invalid;
            }
            sqlite3_free(next_branch_number);
            revision->next_revision = next_branch_revision;
        }
    }

    for (i = 0; i < (size_t)archive->branch_count; ++i) {
        Branch *branch = &archive->branches[i];
        int revision_number;
        if (branch->base_revision > head) {
            goto invalid;
        }
        for (revision_number = 1; revision_number <= branch->count;
             ++revision_number) {
            Revision *revision = &branch->revisions[revision_number - 1];
            int expected_next = revision_number < branch->count
                ? revision_number + 1 : 0;
            if (revision->date == NULL || !revision->next_seen ||
                revision->next_revision != expected_next) {
                goto invalid;
            }
        }
        branch_records += (size_t)branch->count;
    }
    if (branch_records > (size_t)(RCS_MAX_REVISIONS - archive->count)) {
        goto invalid;
    }
    if (!validate_archive_branch_links(archive)) {
        goto invalid;
    }

    if (!expect_line(source, length, &position, "desc") ||
        !parse_quoted(source, length, &position, &description,
                      &description_length)) {
        goto invalid;
    }
    sqlite3_free(description);
    (void)description_length;
    if (position + 2 > length || source[position] != '\n' ||
        source[position + 1] != '\n') {
        goto invalid;
    }
    position += 2;

    while (position < length) {
        Revision *current;
        size_t revision_length;
        while (position < length && source[position] == '\n') {
            ++position;
        }
        if (position == length) {
            break;
        }
        if (!read_line(source, length, &position, &line, &line_length)) {
            goto invalid;
        }
        if (line_length != 0 && line[line_length - 1] == '\r') {
            --line_length;
        }
        revision_length = 0;
        while (revision_length < line_length &&
               line[revision_length] != ' ') {
            ++revision_length;
        }
        if (revision_length == 0 || revision_length != line_length ||
            (current = find_archive_revision(archive, line,
                                             revision_length)) == NULL ||
            current->log != NULL || current->text != NULL ||
            !expect_line(source, length, &position, "log") ||
            !parse_quoted(source, length, &position, &current->log,
                          &current->log_length) ||
            memchr(current->log, '\0', current->log_length) != NULL ||
            !valid_utf8(current->log, current->log_length) ||
            position >= length || source[position++] != '\n' ||
            !expect_line(source, length, &position, "text") ||
            !parse_quoted(source, length, &position, &current->text,
                          &current->text_length) ||
            memchr(current->text, '\0', current->text_length) != NULL ||
            !valid_utf8(current->text, current->text_length) ||
            position >= length || source[position++] != '\n') {
            goto invalid;
        }
    }
    if (position != length) {
        goto invalid;
    }

    for (i = 0; i < (size_t)archive->count; ++i) {
        if (archive->revisions[i].log == NULL ||
            archive->revisions[i].text == NULL) {
            goto invalid;
        }
    }
    for (i = 0; i < (size_t)archive->branch_count; ++i) {
        int revision_number;
        for (revision_number = 0;
             revision_number < archive->branches[i].count;
             ++revision_number) {
            if (archive->branches[i].revisions[revision_number].log == NULL ||
                archive->branches[i].revisions[revision_number].text == NULL) {
                goto invalid;
            }
        }
        if (!normalize_branch_revisions(archive, &archive->branches[i])) {
            goto invalid;
        }
    }
    return 1;

invalid:
    free_archive(archive);
    return 0;
}

static int count_lines(const char *text, size_t length)
{
    size_t i;
    int count = 0;
    for (i = 0; i < length; ++i) {
        if (text[i] == '\n') {
            ++count;
        }
    }
    return count;
}

static int make_reverse_delta(const char *new_content, size_t new_length,
                              const char *old_content, size_t old_length,
                              Buffer *delta)
{
    int new_lines = count_lines(new_content, new_length);
    int old_lines = count_lines(old_content, old_length);
    size_t position = 0;

    if (new_lines > 0) {
        if (!buffer_append_cstr(delta, "d1 ") ||
            !buffer_append_int(delta, new_lines) ||
            !buffer_append_cstr(delta, "\n")) {
            return 0;
        }
    }
    if (old_lines > 0) {
        if (!buffer_append_cstr(delta, "a") ||
            !buffer_append_int(delta, new_lines) ||
            !buffer_append_cstr(delta, " ") ||
            !buffer_append_int(delta, old_lines) ||
            !buffer_append_cstr(delta, "\n")) {
            return 0;
        }
        while (position < old_length) {
            size_t end = position;
            while (end < old_length && old_content[end] != '\n') {
                ++end;
            }
            if (end == old_length ||
                !buffer_append(delta, old_content + position,
                               end - position + 1)) {
                return 0;
            }
            position = end + 1;
        }
    }
    return 1;
}

static int split_lines(const char *text, size_t length, TextLine **lines,
                       int *line_count);
static int append_base_lines(Buffer *output, const TextLine *lines,
                             int start, int end);

static int compare_delta_operations(const void *left, const void *right)
{
    const DeltaOperation *a = (const DeltaOperation *)left;
    const DeltaOperation *b = (const DeltaOperation *)right;
    if (a->position != b->position) {
        return (a->position > b->position) - (a->position < b->position);
    }
    if (a->kind != b->kind) {
        return a->kind == 'a' ? -1 : 1;
    }
    return (a->sequence > b->sequence) - (a->sequence < b->sequence);
}

static int apply_reverse_delta(const char *source, size_t source_length,
                               const char *delta, size_t delta_length,
                               Buffer *result)
{
    size_t position = 0;
    size_t source_position = 0;
    const char *line;
    size_t line_length;
    TextLine *source_lines = NULL;
    int source_line_count;
    DeltaOperation *operations = NULL;
    int operation_count = 0;
    int operation_capacity = 0;
    int i;
    int success = 0;

    result->data = NULL;
    result->length = 0;
    result->capacity = 0;
    if (!split_lines(source, source_length, &source_lines,
                     &source_line_count)) {
        return 0;
    }
    while (position < delta_length) {
        DeltaOperation operation;
        size_t command_end;
        size_t number_end;
        int line_number;
        int count;
        int capacity;
        DeltaOperation *resized;

        if (!read_line(delta, delta_length, &position, &line, &line_length) ||
            line_length < 4 || (line[0] != 'a' && line[0] != 'd')) {
            goto cleanup;
        }
        if (line_length != 0 && line[line_length - 1] == '\r') {
            --line_length;
        }
        number_end = 1;
        while (number_end < line_length && line[number_end] != ' ') {
            ++number_end;
        }
        if (number_end == line_length ||
            !parse_decimal(line + 1, number_end - 1, &line_number)) {
            goto cleanup;
        }
        command_end = number_end + 1;
        while (command_end < line_length && line[command_end] == ' ') {
            ++command_end;
        }
        if (!parse_decimal(line + command_end, line_length - command_end,
                           &count) || count <= 0) {
            goto cleanup;
        }
        memset(&operation, 0, sizeof(operation));
        operation.kind = line[0];
        operation.sequence = operation_count;
        operation.count = count;
        if (operation.kind == 'd') {
            if (line_number <= 0 || line_number > source_line_count ||
                count > source_line_count - (line_number - 1)) {
                goto cleanup;
            }
            operation.position = line_number - 1;
        } else {
            if (line_number < 0 || line_number > source_line_count ||
                count > RCS_MAX_CONTENT) {
                goto cleanup;
            }
            operation.position = line_number;
            operation.added_start = position;
            for (i = 0; i < count; ++i) {
                if (!read_line(delta, delta_length, &position, &line,
                               &line_length)) {
                    goto cleanup;
                }
            }
            operation.added_length = position - operation.added_start;
        }
        if (operation_count == RCS_MAX_DELTA_OPERATIONS) {
            goto cleanup;
        }
        if (operation_count == operation_capacity) {
            capacity = operation_capacity == 0 ? 16 :
                       operation_capacity * 2;
            if (capacity > RCS_MAX_DELTA_OPERATIONS) {
                capacity = RCS_MAX_DELTA_OPERATIONS;
            }
            resized = (DeltaOperation *)sqlite3_realloc64(
                operations, (sqlite3_uint64)capacity *
                            sizeof(DeltaOperation));
            if (resized == NULL) {
                goto cleanup;
            }
            operations = resized;
            operation_capacity = capacity;
        }
        operations[operation_count++] = operation;
    }

    if (operation_count > 1) {
        qsort(operations, (size_t)operation_count, sizeof(DeltaOperation),
              compare_delta_operations);
    }
    for (i = 0; i < operation_count; ++i) {
        const DeltaOperation *operation = &operations[i];
        if (operation->position < (int)source_position ||
            operation->position > source_line_count ||
            !append_base_lines(result, source_lines, (int)source_position,
                               operation->position)) {
            goto cleanup;
        }
        source_position = (size_t)operation->position;
        if (operation->kind == 'a') {
            if (!buffer_append(result, delta + operation->added_start,
                               operation->added_length)) {
                goto cleanup;
            }
        } else {
            source_position += (size_t)operation->count;
        }
    }
    if (!append_base_lines(result, source_lines, (int)source_position,
                           source_line_count) ||
        result->length > RCS_MAX_CONTENT) {
        goto cleanup;
    }
    success = 1;

cleanup:
    sqlite3_free(source_lines);
    sqlite3_free(operations);
    if (!success) {
        sqlite3_free(result->data);
        result->data = NULL;
        result->length = 0;
        result->capacity = 0;
    }
    return success;
}

static int valid_text_input(const unsigned char *text, int length)
{
    return length >= 0 &&
           (length == 0 || (text[length - 1] == '\n' &&
                            memchr(text, '\0', (size_t)length) == NULL &&
                            valid_utf8((const char *)text,
                                       (size_t)length)));
}

static int checkout_revision(const Archive *archive, int target,
                             Buffer *content)
{
    int number;
    content->data = NULL;
    content->length = 0;
    content->capacity = 0;
    if (target < 1 || target > archive->count ||
        !buffer_append(content,
                       archive->revisions[archive->count - 1].text,
                       archive->revisions[archive->count - 1].text_length)) {
        return 0;
    }
    for (number = archive->count; number > target; --number) {
        Buffer older;
        const Revision *revision = &archive->revisions[number - 2];
        older.data = NULL;
        older.length = 0;
        older.capacity = 0;
        if (!apply_reverse_delta(content->data, content->length,
                                 revision->text, revision->text_length,
                                 &older)) {
            sqlite3_free(content->data);
            sqlite3_free(older.data);
            content->data = NULL;
            content->length = 0;
            content->capacity = 0;
            return 0;
        }
        sqlite3_free(content->data);
        *content = older;
    }
    return 1;
}

static int checkout_branch(const Branch *branch, int target, Buffer *content)
{
    content->data = NULL;
    content->length = 0;
    content->capacity = 0;
    if (target < 1 || target > branch->count ||
        !buffer_append(content, branch->revisions[target - 1].text,
                       branch->revisions[target - 1].text_length)) {
        return 0;
    }
    return 1;
}

static int checkout_archive_revision(Archive *archive,
                                    const char *revision_number,
                                    Buffer *content)
{
    int revision;
    char *branch_number = NULL;
    int base_revision;
    int branch_id;
    int branch_revision;
    Branch *branch;
    size_t number_length = strlen(revision_number);
    if (parse_trunk_revision_text(revision_number, number_length, &revision)) {
        return checkout_revision(archive, revision, content);
    }
    if (!parse_branch_revision_path(revision_number, number_length,
                                    &branch_number, &base_revision,
                                    &branch_id, &branch_revision)) {
        return 0;
    }
    branch = find_branch(archive, branch_number);
    sqlite3_free(branch_number);
    return branch != NULL &&
           checkout_branch(branch, branch_revision, content);
}

static int normalize_branch_revisions(Archive *archive, Branch *branch)
{
    Buffer base = {NULL, 0, 0};
    const char *content;
    size_t content_length;
    int content_is_temporary = 1;
    int number;
    if (branch->count == 0) {
        return 1;
    }
    if (!checkout_archive_revision(archive, branch->parent_revision, &base)) {
        return 0;
    }
    content = base.data;
    content_length = base.length;
    for (number = 0; number < branch->count; ++number) {
        Revision *revision = &branch->revisions[number];
        Buffer next = {NULL, 0, 0};
        if (!apply_reverse_delta(content, content_length,
                                 revision->text, revision->text_length,
                                 &next) ||
            next.length > RCS_MAX_CONTENT ||
            !valid_text_input((const unsigned char *)next.data,
                              (int)next.length)) {
            if (content_is_temporary) {
                sqlite3_free(base.data);
            }
            sqlite3_free(next.data);
            return 0;
        }
        if (content_is_temporary) {
            sqlite3_free(base.data);
            base.data = NULL;
            content_is_temporary = 0;
        }
        sqlite3_free(revision->text);
        revision->text = next.data;
        revision->text_length = next.length;
        content = revision->text;
        content_length = revision->text_length;
    }
    return 1;
}

static int validate_revision_chain(const Revision *revisions, int count)
{
    Buffer content = {NULL, 0, 0};
    int number;
    if (count < 1 ||
        revisions[count - 1].text_length > RCS_MAX_CONTENT ||
        !valid_text_input((const unsigned char *)revisions[count - 1].text,
                          (int)revisions[count - 1].text_length) ||
        !buffer_append(&content, revisions[count - 1].text,
                       revisions[count - 1].text_length)) {
        return 0;
    }
    for (number = count; number > 1; --number) {
        Buffer older = {NULL, 0, 0};
        const Revision *revision = &revisions[number - 2];
        if (!apply_reverse_delta(content.data, content.length,
                                 revision->text, revision->text_length,
                                 &older) ||
            older.length > INT_MAX ||
            !valid_text_input((const unsigned char *)older.data,
                              (int)older.length)) {
            sqlite3_free(content.data);
            sqlite3_free(older.data);
            return 0;
        }
        sqlite3_free(content.data);
        content = older;
    }
    sqlite3_free(content.data);
    return 1;
}

static int validate_archive_content(const Archive *archive)
{
    int i;
    if (!validate_revision_chain(archive->revisions, archive->count)) {
        return 0;
    }
    for (i = 0; i < archive->branch_count; ++i) {
        int revision_number;
        for (revision_number = 0;
             revision_number < archive->branches[i].count;
             ++revision_number) {
            const Revision *revision =
                &archive->branches[i].revisions[revision_number];
            if (revision->text_length > RCS_MAX_CONTENT ||
                revision->text_length > INT_MAX ||
                !valid_text_input((const unsigned char *)revision->text,
                                  (int)revision->text_length)) {
                return 0;
            }
        }
    }
    return 1;
}

static Symbol *find_symbol(Archive *archive, const char *name, size_t length)
{
    int i;
    for (i = 0; i < archive->symbol_count; ++i) {
        if (strlen(archive->symbols[i].name) == length &&
            memcmp(archive->symbols[i].name, name, length) == 0) {
            return &archive->symbols[i];
        }
    }
    return NULL;
}

static int checkout_requested_revision(Archive *archive, sqlite3_value *value,
                                       Buffer *content, char **canonical)
{
    const unsigned char *text = sqlite3_value_text(value);
    int length = sqlite3_value_bytes(value);
    int revision;
    int base_revision;
    int branch_id;
    int branch_revision;
    Symbol *symbol;
    const char *number_text;
    size_t number_length;

    *canonical = NULL;
    if (text == NULL) {
        return 0;
    }
    symbol = find_symbol(archive, (const char *)text, (size_t)length);
    number_text = symbol == NULL ? (const char *)text : symbol->revision;
    number_length = symbol == NULL ? (size_t)length : strlen(symbol->revision);

    if (parse_trunk_revision_text(number_text, number_length, &revision)) {
        if (revision > archive->count ||
            !checkout_revision(archive, revision, content)) {
            return 0;
        }
        {
            Buffer result = {NULL, 0, 0};
            if (!buffer_append_cstr(&result, "1.") ||
                !buffer_append_int(&result, revision)) {
                sqlite3_free(result.data);
                sqlite3_free(content->data);
                content->data = NULL;
                content->length = 0;
                content->capacity = 0;
                return 0;
            }
            *canonical = result.data;
        }
        return 1;
    }
    {
        char *branch_number = NULL;
        Branch *branch;
        if (parse_branch_path(number_text, number_length, NULL,
                              &base_revision, &branch_id)) {
            branch_number = copy_bytes(number_text, number_length);
            branch = branch_number == NULL ? NULL :
                     find_branch(archive, branch_number);
            if (branch == NULL || branch->count == 0 ||
                !checkout_branch(branch, branch->count, content)) {
                sqlite3_free(branch_number);
                return 0;
            }
            *canonical = make_branch_revision_path(branch->number,
                                                  branch->count);
            sqlite3_free(branch_number);
            if (*canonical == NULL) {
                sqlite3_free(content->data);
                content->data = NULL;
                content->length = 0;
                content->capacity = 0;
                return 0;
            }
            return 1;
        }
        if (!parse_branch_revision_path(number_text, number_length,
                                        &branch_number, &base_revision,
                                        &branch_id, &branch_revision)) {
            return 0;
        }
        branch = find_branch(archive, branch_number);
        sqlite3_free(branch_number);
        if (branch == NULL || branch_revision > branch->count ||
            !checkout_branch(branch, branch_revision, content)) {
            return 0;
        }
        *canonical = make_branch_revision_path(branch->number,
                                              branch_revision);
    }
    if (*canonical == NULL) {
        sqlite3_free(content->data);
        content->data = NULL;
        content->length = 0;
        content->capacity = 0;
        return 0;
    }
    return 1;
}

static char *resolve_requested_revision(Archive *archive, sqlite3_value *value)
{
    const unsigned char *text = sqlite3_value_text(value);
    int length = sqlite3_value_bytes(value);
    int revision;
    int base_revision;
    int branch_id;
    int branch_revision;
    Symbol *symbol;
    const char *number_text;
    size_t number_length;

    if (text == NULL) {
        return NULL;
    }
    symbol = find_symbol(archive, (const char *)text, (size_t)length);
    number_text = symbol == NULL ? (const char *)text : symbol->revision;
    number_length = symbol == NULL ? (size_t)length : strlen(symbol->revision);
    if (parse_trunk_revision_text(number_text, number_length, &revision)) {
        if (revision > archive->count) {
            return NULL;
        }
        {
            Buffer result = {NULL, 0, 0};
            if (!buffer_append_cstr(&result, "1.") ||
                !buffer_append_int(&result, revision)) {
                sqlite3_free(result.data);
                return NULL;
            }
            return result.data;
        }
    }
    {
        char *branch_number = NULL;
        Branch *branch;
        if (parse_branch_path(number_text, number_length, NULL,
                              &base_revision, &branch_id)) {
            branch_number = copy_bytes(number_text, number_length);
            branch = branch_number == NULL ? NULL :
                     find_branch(archive, branch_number);
            sqlite3_free(branch_number);
            return branch == NULL ? NULL :
                copy_bytes(branch->number, strlen(branch->number));
        }
        if (!parse_branch_revision_path(number_text, number_length,
                                        &branch_number, &base_revision,
                                        &branch_id, &branch_revision)) {
            return NULL;
        }
        branch = find_branch(archive, branch_number);
        sqlite3_free(branch_number);
        if (branch == NULL || branch_revision > branch->count) {
            return NULL;
        }
        return make_branch_revision_path(branch->number, branch_revision);
    }
}

static int split_lines(const char *text, size_t length, TextLine **lines,
                       int *line_count)
{
    size_t position = 0;
    int count = count_lines(text, length);
    int index = 0;

    *lines = NULL;
    *line_count = count;
    if (count == 0) {
        return 1;
    }
    if ((size_t)count > (size_t)-1 / sizeof(TextLine)) {
        return 0;
    }
    *lines = (TextLine *)sqlite3_malloc64(
        (sqlite3_uint64)count * sizeof(TextLine));
    if (*lines == NULL) {
        return 0;
    }
    while (position < length && index < count) {
        size_t end = position;
        while (end < length && text[end] != '\n') {
            ++end;
        }
        if (end == length) {
            sqlite3_free(*lines);
            *lines = NULL;
            return 0;
        }
        (*lines)[index].data = text + position;
        (*lines)[index].length = end - position + 1;
        position = end + 1;
        ++index;
    }
    return position == length && index == count;
}

static int lines_equal(const TextLine *left, const TextLine *right)
{
    return left->length == right->length &&
           memcmp(left->data, right->data, left->length) == 0;
}

static int build_merge_changes(const TextLine *base_lines, int base_count,
                               const TextLine *variant_lines,
                               int variant_count, MergeChange **changes,
                               int *change_count);

static int append_diff_line(Buffer *output, char kind, const TextLine *line)
{
    return buffer_append(output, &kind, 1) &&
           buffer_append(output, line->data, line->length);
}

static int make_unified_diff(const char *left, size_t left_length,
                             const char *left_revision,
                             const char *right, size_t right_length,
                             const char *right_revision, Buffer *output)
{
    const int context = 3;
    TextLine *left_lines = NULL;
    TextLine *right_lines = NULL;
    MergeChange *changes = NULL;
    int change_count = 0;
    int left_count;
    int right_count;
    int status;
    int change_index = 0;

    if (left_length == right_length &&
        memcmp(left, right, left_length) == 0) {
        return 1;
    }
    left_count = count_lines(left, left_length);
    right_count = count_lines(right, right_length);
    if (!split_lines(left, left_length, &left_lines, &left_count)) {
        return 0;
    }
    if (!split_lines(right, right_length, &right_lines, &right_count)) {
        sqlite3_free(left_lines);
        return 0;
    }
    status = build_merge_changes(left_lines, left_count, right_lines,
                                 right_count, &changes, &change_count);
    if (status != 1) {
        sqlite3_free(left_lines);
        sqlite3_free(right_lines);
        sqlite3_free(changes);
        return status;
    }
    if (!buffer_append_cstr(output, "--- ") ||
        !buffer_append_cstr(output, left_revision) ||
        !buffer_append_cstr(output, "\n+++ ") ||
        !buffer_append_cstr(output, right_revision) ||
        !buffer_append(output, "\n", 1)) {
        status = 0;
        goto cleanup;
    }
    while (change_index < change_count) {
        const MergeChange *change = &changes[change_index];
        int old_start = change->start > context
            ? change->start - context : 0;
        int old_end = change->end > left_count - context
            ? left_count : change->end + context;
        int new_start = change->replacement_start > context
            ? change->replacement_start - context : 0;
        int replacement_end =
            change->replacement_start + change->replacement_count;
        int new_end = replacement_end > right_count - context
            ? right_count : replacement_end + context;
        int hunk_change_end = change_index + 1;
        int old_position;
        int new_position;
        int i;

        while (hunk_change_end < change_count) {
            const MergeChange *next = &changes[hunk_change_end];
            int next_old_start = next->start > context
                ? next->start - context : 0;
            int next_new_start = next->replacement_start > context
                ? next->replacement_start - context : 0;
            if (next_old_start > old_end && next_new_start > new_end) {
                break;
            }
            if (next->end + context > old_end) {
                old_end = next->end > left_count - context
                    ? left_count : next->end + context;
            }
            replacement_end =
                next->replacement_start + next->replacement_count;
            if (replacement_end + context > new_end) {
                new_end = replacement_end > right_count - context
                    ? right_count : replacement_end + context;
            }
            ++hunk_change_end;
        }

        if (!buffer_append_cstr(output, "@@ -") ||
            !buffer_append_int(output,
                               old_end == old_start
                                   ? old_start : old_start + 1) ||
            !buffer_append(output, ",", 1) ||
            !buffer_append_int(output, old_end - old_start) ||
            !buffer_append_cstr(output, " +") ||
            !buffer_append_int(output,
                               new_end == new_start
                                   ? new_start : new_start + 1) ||
            !buffer_append(output, ",", 1) ||
            !buffer_append_int(output, new_end - new_start) ||
            !buffer_append_cstr(output, " @@\n")) {
            status = 0;
            goto cleanup;
        }

        old_position = old_start;
        new_position = new_start;
        for (i = change_index; i < hunk_change_end; ++i) {
            const MergeChange *current = &changes[i];
            while (old_position < current->start &&
                   new_position < current->replacement_start) {
                if (!append_diff_line(output, ' ',
                                      &left_lines[old_position])) {
                    status = 0;
                    goto cleanup;
                }
                ++old_position;
                ++new_position;
            }
            if (old_position != current->start ||
                new_position != current->replacement_start) {
                status = 0;
                goto cleanup;
            }
            while (old_position < current->end) {
                if (!append_diff_line(output, '-',
                                      &left_lines[old_position])) {
                    status = 0;
                    goto cleanup;
                }
                ++old_position;
            }
            replacement_end = current->replacement_start +
                              current->replacement_count;
            while (new_position < replacement_end) {
                if (!append_diff_line(output, '+',
                                      &right_lines[new_position])) {
                    status = 0;
                    goto cleanup;
                }
                ++new_position;
            }
        }
        while (old_position < old_end && new_position < new_end) {
            if (!append_diff_line(output, ' ', &left_lines[old_position])) {
                status = 0;
                goto cleanup;
            }
            ++old_position;
            ++new_position;
        }
        if (old_position != old_end || new_position != new_end) {
            status = 0;
            goto cleanup;
        }
        change_index = hunk_change_end;
    }

    status = 1;

cleanup:
    sqlite3_free(changes);
    sqlite3_free(left_lines);
    sqlite3_free(right_lines);
    return status;
}

static int append_merge_change(MergeChange **changes, int *count,
                              int *capacity, MergeChange change)
{
    MergeChange *resized;
    int next_capacity;
    if (*count == *capacity) {
        next_capacity = *capacity == 0 ? 16 : *capacity * 2;
        if (next_capacity < *capacity ||
            (size_t)next_capacity > (size_t)-1 / sizeof(MergeChange)) {
            return 0;
        }
        resized = (MergeChange *)sqlite3_realloc64(
            *changes, (sqlite3_uint64)next_capacity * sizeof(MergeChange));
        if (resized == NULL) {
            return 0;
        }
        *changes = resized;
        *capacity = next_capacity;
    }
    (*changes)[(*count)++] = change;
    return 1;
}

static int build_merge_changes(const TextLine *base_lines, int base_count,
                               const TextLine *variant_lines,
                               int variant_count, MergeChange **changes,
                               int *change_count)
{
    size_t rows = (size_t)base_count + 1;
    size_t columns = (size_t)variant_count + 1;
    size_t cells;
    uint32_t *lcs;
    int capacity = 0;
    size_t i;
    size_t j;
    int active = 0;
    MergeChange current = {0, 0, 0, 0};

    *changes = NULL;
    *change_count = 0;
    if (rows > (size_t)-1 / columns) {
        return -1;
    }
    cells = rows * columns;
    if (cells > RCS_MAX_DIFF_CELLS ||
        cells > (size_t)-1 / sizeof(uint32_t)) {
        return -1;
    }
    lcs = (uint32_t *)sqlite3_malloc64(
        (sqlite3_uint64)cells * sizeof(uint32_t));
    if (lcs == NULL) {
        return 0;
    }
    memset(lcs, 0, cells * sizeof(uint32_t));
    for (i = (size_t)base_count; i > 0; --i) {
        for (j = (size_t)variant_count; j > 0; --j) {
            size_t row = i - 1;
            size_t column = j - 1;
            if (lines_equal(&base_lines[row], &variant_lines[column])) {
                lcs[row * columns + column] =
                    lcs[i * columns + j] + 1;
            } else {
                uint32_t below = lcs[i * columns + column];
                uint32_t next = lcs[row * columns + j];
                lcs[row * columns + column] = below > next ? below : next;
            }
        }
    }

    i = 0;
    j = 0;
    while (i < (size_t)base_count || j < (size_t)variant_count) {
        if (i < (size_t)base_count && j < (size_t)variant_count &&
            lines_equal(&base_lines[i], &variant_lines[j])) {
            if (active && !append_merge_change(changes, change_count,
                                                &capacity, current)) {
                sqlite3_free(lcs);
                sqlite3_free(*changes);
                *changes = NULL;
                *change_count = 0;
                return 0;
            }
            active = 0;
            ++i;
            ++j;
        } else if (j < (size_t)variant_count &&
                   (i == (size_t)base_count ||
                    lcs[i * columns + j + 1] >
                    lcs[(i + 1) * columns + j])) {
            if (!active) {
                current.start = (int)i;
                current.end = (int)i;
                current.replacement_start = (int)j;
                current.replacement_count = 0;
                active = 1;
            }
            ++current.replacement_count;
            ++j;
        } else {
            if (!active) {
                current.start = (int)i;
                current.end = (int)i;
                current.replacement_start = (int)j;
                current.replacement_count = 0;
                active = 1;
            }
            current.end = (int)i + 1;
            ++i;
        }
    }
    if (active && !append_merge_change(changes, change_count, &capacity,
                                       current)) {
        sqlite3_free(lcs);
        sqlite3_free(*changes);
        *changes = NULL;
        *change_count = 0;
        return 0;
    }
    sqlite3_free(lcs);
    return 1;
}

static int change_overlaps_region(const MergeChange *change, int start,
                                  int end)
{
    if (change->start == change->end && start == end) {
        return change->start == start;
    }
    if (change->start == change->end) {
        return change->start > start && change->start < end;
    }
    if (start == end) {
        return start > change->start && start < change->end;
    }
    return change->start < end && start < change->end;
}

static int append_base_lines(Buffer *output, const TextLine *lines,
                             int start, int end)
{
    int i;
    for (i = start; i < end; ++i) {
        if (!buffer_append(output, lines[i].data, lines[i].length)) {
            return 0;
        }
    }
    return 1;
}

static int append_changed_lines(Buffer *output, const MergeChange *change,
                                const TextLine *variant_lines)
{
    int i;
    for (i = 0; i < change->replacement_count; ++i) {
        const TextLine *line =
            &variant_lines[change->replacement_start + i];
        if (!buffer_append(output, line->data, line->length)) {
            return 0;
        }
    }
    return 1;
}

static int append_merge_variant(Buffer *output, const TextLine *base_lines,
                                const MergeChange *changes, int first,
                                int end, const TextLine *variant_lines,
                                int start, int finish)
{
    int position = start;
    int i;
    for (i = first; i < end; ++i) {
        if (changes[i].start < position || changes[i].end > finish ||
            !append_base_lines(output, base_lines, position,
                               changes[i].start) ||
            !append_changed_lines(output, &changes[i], variant_lines)) {
            return 0;
        }
        position = changes[i].end;
    }
    return append_base_lines(output, base_lines, position, finish);
}

static int merge_contents(const char *base, size_t base_length,
                          const char *ours, size_t ours_length,
                          const char *theirs, size_t theirs_length,
                          Buffer *merged, int *has_conflicts)
{
    TextLine *base_lines = NULL;
    TextLine *ours_lines = NULL;
    TextLine *theirs_lines = NULL;
    int base_count;
    int ours_count;
    int theirs_count;
    MergeChange *ours_changes = NULL;
    MergeChange *theirs_changes = NULL;
    int ours_change_count = 0;
    int theirs_change_count = 0;
    int ours_index = 0;
    int theirs_index = 0;
    int base_index = 0;
    int status = 0;

    merged->data = NULL;
    merged->length = 0;
    merged->capacity = 0;
    *has_conflicts = 0;
    if (!split_lines(base, base_length, &base_lines, &base_count) ||
        !split_lines(ours, ours_length, &ours_lines, &ours_count) ||
        !split_lines(theirs, theirs_length, &theirs_lines, &theirs_count)) {
        goto cleanup;
    }
    status = build_merge_changes(base_lines, base_count, ours_lines,
                                 ours_count, &ours_changes,
                                 &ours_change_count);
    if (status != 1) {
        goto cleanup;
    }
    status = build_merge_changes(base_lines, base_count, theirs_lines,
                                 theirs_count, &theirs_changes,
                                 &theirs_change_count);
    if (status != 1) {
        goto cleanup;
    }
    status = 0;

    while (ours_index < ours_change_count ||
           theirs_index < theirs_change_count) {
        int take_ours;
        MergeChange *next;
        if (ours_index >= ours_change_count) {
            take_ours = 0;
        } else if (theirs_index >= theirs_change_count) {
            take_ours = 1;
        } else if (ours_changes[ours_index].start !=
                   theirs_changes[theirs_index].start) {
            take_ours = ours_changes[ours_index].start <
                        theirs_changes[theirs_index].start;
        } else {
            int ours_insertion =
                ours_changes[ours_index].start == ours_changes[ours_index].end;
            int theirs_insertion =
                theirs_changes[theirs_index].start ==
                theirs_changes[theirs_index].end;
            take_ours = ours_insertion || !theirs_insertion;
        }

        if (ours_index < ours_change_count &&
            theirs_index < theirs_change_count &&
            change_overlaps_region(&ours_changes[ours_index],
                                   theirs_changes[theirs_index].start,
                                   theirs_changes[theirs_index].end) &&
            change_overlaps_region(&theirs_changes[theirs_index],
                                   ours_changes[ours_index].start,
                                   ours_changes[ours_index].end)) {
            int cluster_start = ours_changes[ours_index].start <
                    theirs_changes[theirs_index].start
                ? ours_changes[ours_index].start
                : theirs_changes[theirs_index].start;
            int cluster_end = ours_changes[ours_index].end >
                    theirs_changes[theirs_index].end
                ? ours_changes[ours_index].end
                : theirs_changes[theirs_index].end;
            int ours_first = ours_index;
            int theirs_first = theirs_index;
            int ours_end;
            int theirs_end;
            int expanded;
            Buffer ours_variant = {NULL, 0, 0};
            Buffer theirs_variant = {NULL, 0, 0};

            ++ours_index;
            ++theirs_index;
            do {
                expanded = 0;
                while (ours_index < ours_change_count &&
                       change_overlaps_region(&ours_changes[ours_index],
                                              cluster_start, cluster_end)) {
                    if (ours_changes[ours_index].start < cluster_start) {
                        cluster_start = ours_changes[ours_index].start;
                    }
                    if (ours_changes[ours_index].end > cluster_end) {
                        cluster_end = ours_changes[ours_index].end;
                    }
                    ++ours_index;
                    expanded = 1;
                }
                while (theirs_index < theirs_change_count &&
                       change_overlaps_region(&theirs_changes[theirs_index],
                                              cluster_start, cluster_end)) {
                    if (theirs_changes[theirs_index].start < cluster_start) {
                        cluster_start = theirs_changes[theirs_index].start;
                    }
                    if (theirs_changes[theirs_index].end > cluster_end) {
                        cluster_end = theirs_changes[theirs_index].end;
                    }
                    ++theirs_index;
                    expanded = 1;
                }
            } while (expanded);
            ours_end = ours_index;
            theirs_end = theirs_index;
            if (!append_base_lines(merged, base_lines, base_index,
                                   cluster_start) ||
                !append_merge_variant(&ours_variant, base_lines,
                                      ours_changes, ours_first, ours_end,
                                      ours_lines, cluster_start,
                                      cluster_end) ||
                !append_merge_variant(&theirs_variant, base_lines,
                                      theirs_changes, theirs_first,
                                      theirs_end, theirs_lines,
                                      cluster_start, cluster_end)) {
                sqlite3_free(ours_variant.data);
                sqlite3_free(theirs_variant.data);
                goto cleanup;
            }
            if (ours_variant.length == theirs_variant.length &&
                (ours_variant.length == 0 ||
                 memcmp(ours_variant.data, theirs_variant.data,
                        ours_variant.length) == 0)) {
                if (!buffer_append(merged, ours_variant.data,
                                   ours_variant.length)) {
                    sqlite3_free(ours_variant.data);
                    sqlite3_free(theirs_variant.data);
                    goto cleanup;
                }
            } else {
                if (!buffer_append_cstr(merged, "<<<<<<< ours\n") ||
                    !buffer_append(merged, ours_variant.data,
                                   ours_variant.length) ||
                    !buffer_append_cstr(merged, "=======\n") ||
                    !buffer_append(merged, theirs_variant.data,
                                   theirs_variant.length) ||
                    !buffer_append_cstr(merged, ">>>>>>> theirs\n")) {
                    sqlite3_free(ours_variant.data);
                    sqlite3_free(theirs_variant.data);
                    goto cleanup;
                }
                *has_conflicts = 1;
            }
            sqlite3_free(ours_variant.data);
            sqlite3_free(theirs_variant.data);
            base_index = cluster_end;
            continue;
        }

        next = take_ours ? &ours_changes[ours_index]
                         : &theirs_changes[theirs_index];
        if (!append_base_lines(merged, base_lines, base_index, next->start) ||
            !append_changed_lines(merged, next,
                                  take_ours ? ours_lines : theirs_lines)) {
            goto cleanup;
        }
        base_index = next->end;
        if (take_ours) {
            ++ours_index;
        } else {
            ++theirs_index;
        }
    }
    if (!append_base_lines(merged, base_lines, base_index, base_count)) {
        goto cleanup;
    }
    status = merged->length <= RCS_MAX_CONTENT ? 1 : -2;

cleanup:
    sqlite3_free(base_lines);
    sqlite3_free(ours_lines);
    sqlite3_free(theirs_lines);
    sqlite3_free(ours_changes);
    sqlite3_free(theirs_changes);
    if (status != 1) {
        sqlite3_free(merged->data);
        merged->data = NULL;
        merged->length = 0;
        merged->capacity = 0;
    }
    return status;
}

static int append_quoted(Buffer *buffer, const char *text, size_t length)
{
    size_t position = 0;
    if (!buffer_append(buffer, "@", 1)) {
        return 0;
    }
    while (position < length) {
        const char *at = (const char *)memchr(text + position, '@',
                                              length - position);
        size_t chunk = at == NULL ? length - position :
                       (size_t)(at - (text + position));
        if (!buffer_append(buffer, text + position, chunk)) {
            return 0;
        }
        position += chunk;
        if (at != NULL) {
            if (!buffer_append(buffer, "@@", 2)) {
                return 0;
            }
            ++position;
        }
    }
    return buffer_append(buffer, "@", 1);
}

static int append_json_string(Buffer *buffer, const char *text, size_t length)
{
    static const char hex[] = "0123456789abcdef";
    size_t i;
    if (!buffer_append(buffer, "\"", 1)) {
        return 0;
    }
    for (i = 0; i < length; ++i) {
        unsigned char character = (unsigned char)text[i];
        const char *escape = NULL;
        switch (character) {
        case '"': escape = "\\\""; break;
        case '\\': escape = "\\\\"; break;
        case '\b': escape = "\\b"; break;
        case '\f': escape = "\\f"; break;
        case '\n': escape = "\\n"; break;
        case '\r': escape = "\\r"; break;
        case '\t': escape = "\\t"; break;
        default: break;
        }
        if (escape != NULL) {
            if (!buffer_append_cstr(buffer, escape)) {
                return 0;
            }
        } else if (character < 0x20) {
            char escaped[6];
            escaped[0] = '\\';
            escaped[1] = 'u';
            escaped[2] = '0';
            escaped[3] = '0';
            escaped[4] = hex[character >> 4];
            escaped[5] = hex[character & 0x0f];
            if (!buffer_append(buffer, escaped, sizeof(escaped))) {
                return 0;
            }
        } else if (!buffer_append(buffer, text + i, 1)) {
            return 0;
        }
    }
    return buffer_append(buffer, "\"", 1);
}

static int append_metadata_record(Buffer *buffer, const char *number,
                                  const Revision *revision,
                                  const char *branches, const char *next)
{
    if (!buffer_append_cstr(buffer, number) ||
        !buffer_append_cstr(buffer, "\ndate ") ||
        !buffer_append_cstr(buffer, revision->date) ||
        !buffer_append_cstr(buffer, "; author ") ||
        !buffer_append_cstr(buffer, revision->author) ||
        !buffer_append_cstr(buffer, "; state ") ||
        !buffer_append_cstr(buffer, revision->state) ||
        !buffer_append_cstr(buffer, ";\nbranches")) {
        return 0;
    }
    if (branches != NULL && branches[0] != '\0' &&
        (!buffer_append(buffer, " ", 1) ||
         !buffer_append_cstr(buffer, branches))) {
        return 0;
    }
    return buffer_append_cstr(buffer, ";\nnext ") &&
           (next == NULL || buffer_append_cstr(buffer, next)) &&
           buffer_append_cstr(buffer, ";\n\n");
}

static int append_metadata(Buffer *buffer, int number,
                           const Revision *revision, const Archive *archive)
{
    Buffer branches = {NULL, 0, 0};
    Buffer revision_name = {NULL, 0, 0};
    char revision_number[32];
    char next_revision[32];
    int i;
    int branch_count = 0;
    if (!buffer_append_cstr(&revision_name, "1.") ||
        !buffer_append_int(&revision_name, number)) {
        sqlite3_free(revision_name.data);
        return 0;
    }
    sqlite3_snprintf((int)sizeof(revision_number), revision_number, "1.%d",
                     number);
    if (number > 1) {
        sqlite3_snprintf((int)sizeof(next_revision), next_revision,
                         "1.%d", number - 1);
    }
    for (i = 0; i < archive->branch_count; ++i) {
        const Branch *branch = &archive->branches[i];
        char *first_revision;
        if (strcmp(branch->parent_revision, revision_name.data) != 0 ||
            branch->count == 0) {
            continue;
        }
        first_revision = make_branch_revision_path(branch->number, 1);
        if (first_revision == NULL ||
            (branch_count != 0 && !buffer_append(&branches, " ", 1)) ||
            !buffer_append_cstr(&branches, first_revision)) {
            sqlite3_free(first_revision);
            sqlite3_free(branches.data);
            sqlite3_free(revision_name.data);
            return 0;
        }
        sqlite3_free(first_revision);
        ++branch_count;
    }
    if (!append_metadata_record(buffer, revision_number, revision,
                                branches.data,
                                number > 1 ? next_revision : NULL)) {
        sqlite3_free(branches.data);
        sqlite3_free(revision_name.data);
        return 0;
    }
    sqlite3_free(branches.data);
    sqlite3_free(revision_name.data);
    return 1;
}

static int append_branch_metadata(Buffer *buffer, const Archive *archive,
                                  const Branch *branch, int revision_number)
{
    Buffer child_branches = {NULL, 0, 0};
    char *revision_name = make_branch_revision_path(branch->number,
                                                    revision_number);
    char *number = make_branch_revision_path(branch->number, revision_number);
    char *next = revision_number < branch->count
        ? make_branch_revision_path(branch->number, revision_number + 1)
        : NULL;
    int i;
    int child_count = 0;
    int result;
    if (revision_name == NULL || number == NULL ||
        (revision_number < branch->count && next == NULL)) {
        sqlite3_free(revision_name);
        sqlite3_free(number);
        sqlite3_free(next);
        return 0;
    }
    for (i = 0; i < archive->branch_count; ++i) {
        const Branch *child = &archive->branches[i];
        char *first_revision;
        if (child->count == 0 ||
            strcmp(child->parent_revision, revision_name) != 0) {
            continue;
        }
        first_revision = make_branch_revision_path(child->number, 1);
        if (first_revision == NULL ||
            (child_count != 0 &&
             !buffer_append(&child_branches, " ", 1)) ||
            !buffer_append_cstr(&child_branches, first_revision)) {
            sqlite3_free(first_revision);
            sqlite3_free(revision_name);
            sqlite3_free(number);
            sqlite3_free(next);
            sqlite3_free(child_branches.data);
            return 0;
        }
        sqlite3_free(first_revision);
        ++child_count;
    }
    result =
        (revision_number == branch->count || next != NULL) &&
        append_metadata_record(buffer, number,
                               &branch->revisions[revision_number - 1],
                               child_branches.data, next);
    sqlite3_free(revision_name);
    sqlite3_free(number);
    sqlite3_free(next);
    sqlite3_free(child_branches.data);
    return result;
}

static int append_branch_deltatext(Buffer *output, Archive *archive,
                                   const Branch *branch)
{
    Buffer previous = {NULL, 0, 0};
    int revision_number;
    if (branch->count == 0) {
        return 1;
    }
    if (!checkout_archive_revision(archive, branch->parent_revision,
                                   &previous)) {
        return 0;
    }
    for (revision_number = 1; revision_number <= branch->count;
         ++revision_number) {
        const Revision *revision = &branch->revisions[revision_number - 1];
        Buffer delta = {NULL, 0, 0};
        char *revision_name = make_branch_revision_path(branch->number,
                                                        revision_number);
        Buffer current = {NULL, 0, 0};
        if (revision_name == NULL ||
            !make_reverse_delta(previous.data, previous.length,
                                revision->text, revision->text_length,
                                &delta) ||
            !buffer_append_cstr(output, revision_name) ||
            !buffer_append_cstr(output, "\nlog\n") ||
            !append_quoted(output, revision->log, revision->log_length) ||
            !buffer_append_cstr(output, "\ntext\n") ||
            !append_quoted(output, delta.data, delta.length) ||
            !buffer_append_cstr(output, "\n") ||
            !buffer_append(&current, revision->text,
                           revision->text_length)) {
            sqlite3_free(revision_name);
            sqlite3_free(delta.data);
            sqlite3_free(current.data);
            sqlite3_free(previous.data);
            return 0;
        }
        sqlite3_free(revision_name);
        sqlite3_free(delta.data);
        sqlite3_free(previous.data);
        previous = current;
    }
    sqlite3_free(previous.data);
    return 1;
}

static int append_symbols_line(Buffer *buffer, const Archive *archive)
{
    int i;
    if (!buffer_append_cstr(buffer, "symbols")) {
        return 0;
    }
    if (archive->symbol_count == 0) {
        return buffer_append_cstr(buffer, ";\n");
    }
    for (i = 0; i < archive->symbol_count; ++i) {
        if (!buffer_append_cstr(buffer, " ") ||
            !buffer_append_cstr(buffer, archive->symbols[i].name) ||
            !buffer_append_cstr(buffer, ": ") ||
            !buffer_append_cstr(buffer, archive->symbols[i].revision)) {
            return 0;
        }
    }
    return buffer_append_cstr(buffer, ";\n");
}

static int serialize_archive(Archive *archive, Buffer *output)
{
    int number;
    size_t branch_records = 0;
    size_t branch_record_number;
    if (!buffer_append_cstr(output, "head 1.") ||
        !buffer_append_int(output, archive->count) ||
        !buffer_append_cstr(output, ";\naccess;\n") ||
        !append_symbols_line(output, archive) ||
        !buffer_append_cstr(output,
            "locks; strict;\ncomment @# @;\n\n")) {
        return 0;
    }
    for (number = archive->count; number >= 1; --number) {
        if (!append_metadata(output, number,
                             &archive->revisions[number - 1], archive)) {
            return 0;
        }
    }
    for (number = 0; number < archive->branch_count; ++number) {
        const Branch *branch = &archive->branches[number];
        int revision_number;
        for (revision_number = 1; revision_number <= branch->count;
             ++revision_number) {
            if (!append_branch_metadata(output, archive, branch,
                                        revision_number)) {
                return 0;
            }
        }
        branch_records += (size_t)branch->count;
    }
    if (!buffer_append_cstr(output, "desc\n@SQLiteRCS archive@\n\n")) {
        return 0;
    }
    for (number = archive->count; number >= 1; --number) {
        const Revision *revision = &archive->revisions[number - 1];
        if (!buffer_append_cstr(output, "1.") ||
            !buffer_append_int(output, number) ||
            !buffer_append_cstr(output, "\nlog\n") ||
            !append_quoted(output, revision->log, revision->log_length) ||
            !buffer_append_cstr(output, "\ntext\n") ||
            !append_quoted(output, revision->text, revision->text_length) ||
            !buffer_append_cstr(output, "\n")) {
            return 0;
        }
        if (number > 1 && !buffer_append(output, "\n", 1)) {
            return 0;
        }
    }
    if (branch_records != 0 && !buffer_append(output, "\n", 1)) {
        return 0;
    }
    branch_record_number = 0;
    for (number = 0; number < archive->branch_count; ++number) {
        const Branch *branch = &archive->branches[number];
        if (branch->count > 0) {
            if (branch_record_number != 0 &&
                !buffer_append(output, "\n", 1)) {
                return 0;
            }
            if (!append_branch_deltatext(output, archive, branch)) {
                return 0;
            }
            branch_record_number += (size_t)branch->count;
        }
    }
    return output->length <= RCS_MAX_ARCHIVE;
}

static int make_date(char output[32])
{
    time_t now = time(NULL);
    struct tm value;
    if (now == (time_t)-1) {
        return 0;
    }
#if defined(_WIN32)
    if (gmtime_s(&value, &now) != 0) {
        return 0;
    }
#else
    if (gmtime_r(&now, &value) == NULL) {
        return 0;
    }
#endif
    sqlite3_snprintf(32, output, "%04d.%02d.%02d.%02d.%02d.%02d",
                     value.tm_year + 1900, value.tm_mon + 1, value.tm_mday,
                     value.tm_hour, value.tm_min, value.tm_sec);
    return 1;
}

static int archive_revision_count(const Archive *archive);

static void head_function(sqlite3_context *context, int argc,
                          sqlite3_value **argv)
{
    Archive archive;
    Buffer result;
    const unsigned char *archive_text;
    int archive_length;

    (void)argc;
    archive_text = sqlite3_value_text(argv[0]);
    archive_length = sqlite3_value_bytes(argv[0]);
    if (archive_text == NULL ||
        !parse_archive((const char *)archive_text, (size_t)archive_length,
                       &archive)) {
        set_error(context, "rcs_head received an invalid or unsupported RCS archive");
        return;
    }
    result.data = NULL;
    result.length = 0;
    result.capacity = 0;
    if (!buffer_append_cstr(&result, "1.") ||
        !buffer_append_int(&result, archive.count)) {
        free_archive(&archive);
        sqlite3_free(result.data);
        set_error(context, "failed to create RCS head revision");
        return;
    }
    free_archive(&archive);
    sqlite3_result_text64(context, result.data, (sqlite3_uint64)result.length,
                          sqlite3_free, SQLITE_UTF8);
}

static void revisions_function(sqlite3_context *context, int argc,
                               sqlite3_value **argv)
{
    Archive archive;
    Buffer result = {NULL, 0, 0};
    const unsigned char *archive_text;
    int archive_length;
    int number;
    int i;
    int has_revision = 0;

    (void)argc;
    archive_text = sqlite3_value_text(argv[0]);
    archive_length = sqlite3_value_bytes(argv[0]);
    if (archive_text == NULL ||
        !parse_archive((const char *)archive_text, (size_t)archive_length,
                       &archive)) {
        set_error(context, "rcs_revisions received an invalid or unsupported RCS archive");
        return;
    }
    if (!buffer_append(&result, "[", 1)) {
        free_archive(&archive);
        set_error(context, "failed to create RCS revision list");
        return;
    }
    for (number = archive.count; number >= 1; --number) {
        const Revision *revision = &archive.revisions[number - 1];
        Buffer revision_number = {NULL, 0, 0};
        int success =
            buffer_append_cstr(&revision_number, "1.") &&
            buffer_append_int(&revision_number, number) &&
            (!has_revision || buffer_append(&result, ",", 1)) &&
            buffer_append_cstr(&result, "{\"revision\":") &&
            append_json_string(&result, revision_number.data,
                               revision_number.length) &&
            buffer_append_cstr(&result, ",\"author\":") &&
            append_json_string(&result, revision->author,
                               strlen(revision->author));
        if (success) {
            success = buffer_append_cstr(&result, ",\"date\":") &&
                append_json_string(&result, revision->date,
                                   strlen(revision->date)) &&
                buffer_append_cstr(&result, ",\"state\":") &&
                append_json_string(&result, revision->state,
                                   strlen(revision->state)) &&
                buffer_append_cstr(&result, ",\"log\":") &&
                append_json_string(&result, revision->log,
                                   revision->log_length) &&
                buffer_append_cstr(&result, "}");
        }
        sqlite3_free(revision_number.data);
        if (!success) {
            free_archive(&archive);
            sqlite3_free(result.data);
            set_error(context, "failed to create RCS revision list");
            return;
        }
        has_revision = 1;
        if (result.length > RCS_MAX_ARCHIVE) {
            free_archive(&archive);
            sqlite3_free(result.data);
            set_error(context, "RCS revision list exceeds the output limit");
            return;
        }
    }
    for (i = 0; i < archive.branch_count; ++i) {
        const Branch *branch = &archive.branches[i];
        for (number = branch->count; number >= 1; --number) {
            const Revision *revision = &branch->revisions[number - 1];
            char *revision_number = make_branch_revision_path(
                branch->number, number);
            int success = revision_number != NULL &&
                (!has_revision || buffer_append(&result, ",", 1)) &&
                buffer_append_cstr(&result, "{\"revision\":") &&
                append_json_string(&result, revision_number,
                                   strlen(revision_number)) &&
                buffer_append_cstr(&result, ",\"author\":") &&
                append_json_string(&result, revision->author,
                                   strlen(revision->author)) &&
                buffer_append_cstr(&result, ",\"date\":") &&
                append_json_string(&result, revision->date,
                                   strlen(revision->date)) &&
                buffer_append_cstr(&result, ",\"state\":") &&
                append_json_string(&result, revision->state,
                                   strlen(revision->state)) &&
                buffer_append_cstr(&result, ",\"log\":") &&
                append_json_string(&result, revision->log,
                                   revision->log_length) &&
                buffer_append_cstr(&result, "}");
            sqlite3_free(revision_number);
            if (!success) {
                free_archive(&archive);
                sqlite3_free(result.data);
                set_error(context, "failed to create RCS revision list");
                return;
            }
            has_revision = 1;
            if (result.length > RCS_MAX_ARCHIVE) {
                free_archive(&archive);
                sqlite3_free(result.data);
                set_error(context, "RCS revision list exceeds the output limit");
                return;
            }
        }
    }
    if (!buffer_append(&result, "]", 1)) {
        free_archive(&archive);
        sqlite3_free(result.data);
        set_error(context, "failed to create RCS revision list");
        return;
    }
    free_archive(&archive);
    sqlite3_result_text64(context, result.data, (sqlite3_uint64)result.length,
                          sqlite3_free, SQLITE_UTF8);
}

static int set_symbol(Archive *archive, const char *name, size_t name_length,
                      const char *revision, int is_branch)
{
    int i;
    int position = 0;
    char *copy;
    char *revision_copy;

    revision_copy = copy_bytes(revision, strlen(revision));
    if (revision_copy == NULL) {
        return 0;
    }
    for (i = 0; i < archive->symbol_count; ++i) {
        int comparison = strcmp(archive->symbols[i].name, name);
        if (comparison == 0) {
            sqlite3_free(archive->symbols[i].revision);
            archive->symbols[i].revision = revision_copy;
            archive->symbols[i].is_branch = is_branch;
            return 1;
        }
        if (comparison < 0) {
            position = i + 1;
        }
    }
    if (archive->symbol_count >= RCS_MAX_REVISIONS ||
        !reserve_symbols(archive, archive->symbol_count + 1)) {
        sqlite3_free(revision_copy);
        return 0;
    }
    copy = copy_bytes(name, name_length);
    if (copy == NULL) {
        sqlite3_free(revision_copy);
        return 0;
    }
    memmove(&archive->symbols[position + 1], &archive->symbols[position],
            (size_t)(archive->symbol_count - position) * sizeof(Symbol));
    archive->symbols[position].name = copy;
    archive->symbols[position].revision = revision_copy;
    archive->symbols[position].is_branch = is_branch;
    ++archive->symbol_count;
    return 1;
}

static void tag_function(sqlite3_context *context, int argc,
                         sqlite3_value **argv)
{
    Archive archive;
    Buffer output;
    const unsigned char *archive_text;
    const unsigned char *name;
    int archive_length;
    int name_length;
    int revision;
    Buffer revision_number;
    Symbol *existing;

    (void)argc;
    archive_text = sqlite3_value_text(argv[0]);
    archive_length = sqlite3_value_bytes(argv[0]);
    name = sqlite3_value_text(argv[1]);
    name_length = sqlite3_value_bytes(argv[1]);
    if (archive_text == NULL || name == NULL || name_length > 128 ||
        !valid_symbol_name((const char *)name, (size_t)name_length)) {
        set_error(context, "rcs_tag expects an archive and a valid symbol name");
        return;
    }
    if (!parse_archive((const char *)archive_text, (size_t)archive_length,
                       &archive)) {
        set_error(context, "rcs_tag received an invalid or unsupported RCS archive");
        return;
    }
    if (!resolve_revision(&archive, argv[2], &revision)) {
        free_archive(&archive);
        set_error(context, "RCS revision or symbol does not exist");
        return;
    }
    existing = find_symbol(&archive, (const char *)name, (size_t)name_length);
    if (existing != NULL && existing->is_branch) {
        free_archive(&archive);
        set_error(context, "an RCS branch symbol cannot be replaced by a tag");
        return;
    }
    revision_number.data = NULL;
    revision_number.length = 0;
    revision_number.capacity = 0;
    if (!buffer_append_cstr(&revision_number, "1.") ||
        !buffer_append_int(&revision_number, revision)) {
        free_archive(&archive);
        sqlite3_free(revision_number.data);
        set_error(context, "failed to create RCS tag revision number");
        return;
    }
    output.data = NULL;
    output.length = 0;
    output.capacity = 0;
    if (!set_symbol(&archive, (const char *)name, (size_t)name_length,
                    revision_number.data, 0) ||
        !serialize_archive(&archive, &output)) {
        free_archive(&archive);
        sqlite3_free(revision_number.data);
        sqlite3_free(output.data);
        set_error(context, "failed to update RCS symbol");
        return;
    }
    sqlite3_free(revision_number.data);
    free_archive(&archive);
    sqlite3_result_text64(context, output.data, (sqlite3_uint64)output.length,
                          sqlite3_free, SQLITE_UTF8);
}

static void symbols_function(sqlite3_context *context, int argc,
                             sqlite3_value **argv)
{
    Archive archive;
    Buffer result;
    const unsigned char *archive_text;
    int archive_length;
    int i;

    (void)argc;
    archive_text = sqlite3_value_text(argv[0]);
    archive_length = sqlite3_value_bytes(argv[0]);
    if (archive_text == NULL ||
        !parse_archive((const char *)archive_text, (size_t)archive_length,
                       &archive)) {
        set_error(context, "rcs_symbols received an invalid or unsupported RCS archive");
        return;
    }
    result.data = NULL;
    result.length = 0;
    result.capacity = 0;
    if (!buffer_append(&result, "[", 1)) {
        free_archive(&archive);
        set_error(context, "failed to create RCS symbol list");
        return;
    }
    for (i = 0; i < archive.symbol_count; ++i) {
        const Symbol *symbol = &archive.symbols[i];
        if ((i != 0 && !buffer_append(&result, ",", 1)) ||
            !buffer_append(&result, "{\"name\":", 8) ||
            !append_json_string(&result, symbol->name,
                                strlen(symbol->name)) ||
            !buffer_append(&result, ",\"revision\":", 12) ||
            !append_json_string(&result, symbol->revision,
                                strlen(symbol->revision)) ||
            !buffer_append_cstr(&result, ",\"is_branch\":") ||
            !buffer_append_cstr(&result,
                                symbol->is_branch ? "true}" : "false}")) {
            free_archive(&archive);
            sqlite3_free(result.data);
            set_error(context, "failed to create RCS symbol list");
            return;
        }
    }
    if (!buffer_append(&result, "]", 1) ||
        result.length > RCS_MAX_ARCHIVE) {
        free_archive(&archive);
        sqlite3_free(result.data);
        set_error(context, "RCS symbol list exceeds the output limit");
        return;
    }
    free_archive(&archive);
    sqlite3_result_text64(context, result.data, (sqlite3_uint64)result.length,
                          sqlite3_free, SQLITE_UTF8);
}

static void resolve_function(sqlite3_context *context, int argc,
                             sqlite3_value **argv)
{
    Archive archive;
    Buffer result;
    const unsigned char *archive_text;
    int archive_length;
    char *revision;

    (void)argc;
    archive_text = sqlite3_value_text(argv[0]);
    archive_length = sqlite3_value_bytes(argv[0]);
    if (archive_text == NULL ||
        !parse_archive((const char *)archive_text, (size_t)archive_length,
                       &archive)) {
        set_error(context, "rcs_resolve_revision received an invalid or unsupported RCS archive");
        return;
    }
    revision = resolve_requested_revision(&archive, argv[1]);
    if (revision == NULL) {
        free_archive(&archive);
        set_error(context, "RCS revision or symbol does not exist");
        return;
    }
    result.data = NULL;
    result.length = 0;
    result.capacity = 0;
    if (!buffer_append_cstr(&result, revision)) {
        free_archive(&archive);
        sqlite3_free(revision);
        sqlite3_free(result.data);
        set_error(context, "failed to resolve RCS revision");
        return;
    }
    sqlite3_free(revision);
    free_archive(&archive);
    sqlite3_result_text64(context, result.data, (sqlite3_uint64)result.length,
                          sqlite3_free, SQLITE_UTF8);
}

static void is_valid_function(sqlite3_context *context, int argc,
                              sqlite3_value **argv)
{
    Archive archive;
    const unsigned char *archive_text;
    int archive_length;

    (void)argc;
    if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
        sqlite3_result_null(context);
        return;
    }
    archive_text = sqlite3_value_text(argv[0]);
    if (archive_text == NULL) {
        sqlite3_result_error_nomem(context);
        return;
    }
    archive_length = sqlite3_value_bytes(argv[0]);
    if (archive_length == 0) {
        sqlite3_result_int(context, 1);
        return;
    }
    if (!parse_archive((const char *)archive_text, (size_t)archive_length,
                       &archive)) {
        sqlite3_result_int(context, 0);
        return;
    }
    if (!validate_archive_content(&archive)) {
        free_archive(&archive);
        sqlite3_result_int(context, 0);
        return;
    }
    free_archive(&archive);
    sqlite3_result_int(context, 1);
}

static void normalize_archive_function(sqlite3_context *context,
                                       sqlite3_value **argv,
                                       const char *operation)
{
    Archive archive;
    Buffer output = {NULL, 0, 0};
    const unsigned char *archive_text;
    int archive_length;

    if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
        sqlite3_result_null(context);
        return;
    }
    archive_text = sqlite3_value_text(argv[0]);
    if (archive_text == NULL) {
        sqlite3_result_error_nomem(context);
        return;
    }
    archive_length = sqlite3_value_bytes(argv[0]);
    if (archive_length == 0) {
        sqlite3_result_text(context, "", 0, SQLITE_STATIC);
        return;
    }
    if (!parse_archive((const char *)archive_text, (size_t)archive_length,
                       &archive)) {
        set_error(context, operation);
        return;
    }
    if (!validate_archive_content(&archive)) {
        free_archive(&archive);
        set_error(context, operation);
        return;
    }
    if (!serialize_archive(&archive, &output)) {
        free_archive(&archive);
        sqlite3_free(output.data);
        set_error(context, "failed to serialize normalized RCS archive");
        return;
    }
    free_archive(&archive);
    sqlite3_result_text64(context, output.data,
                          (sqlite3_uint64)output.length,
                          sqlite3_free, SQLITE_UTF8);
}

static void import_function(sqlite3_context *context, int argc,
                            sqlite3_value **argv)
{
    (void)argc;
    normalize_archive_function(context, argv,
        "rcs_import received an invalid or unsupported RCS archive");
}

static void export_function(sqlite3_context *context, int argc,
                            sqlite3_value **argv)
{
    (void)argc;
    normalize_archive_function(context, argv,
        "rcs_export received an invalid or unsupported RCS archive");
}

static void checkin_function(sqlite3_context *context, int argc,
                             sqlite3_value **argv)
{
    Archive archive;
    Buffer delta;
    Buffer output;
    const unsigned char *archive_text;
    const unsigned char *content;
    const unsigned char *log;
    int archive_length;
    int content_length;
    int log_length;
    int old_count;
    Revision *new_revision;
    char date[32];
    int is_empty;
    int archive_type;

    (void)argc;
    archive_type = sqlite3_value_type(argv[0]);
    archive_text = sqlite3_value_text(argv[0]);
    archive_length = sqlite3_value_bytes(argv[0]);
    content = sqlite3_value_text(argv[1]);
    content_length = sqlite3_value_bytes(argv[1]);
    log = sqlite3_value_text(argv[2]);
    log_length = sqlite3_value_bytes(argv[2]);
    if (archive_text == NULL && archive_type != SQLITE_NULL) {
        set_error(context, "failed to read the RCS archive value");
        return;
    }
    is_empty = archive_type == SQLITE_NULL ||
               (archive_text != NULL && archive_length == 0);

    if (content == NULL || log == NULL ||
        !valid_text_input(content, content_length) ||
        (log_length != 0 &&
         (memchr(log, '\0', (size_t)log_length) != NULL ||
          !valid_utf8((const char *)log, (size_t)log_length))) ||
        content_length > RCS_MAX_CONTENT || log_length > RCS_MAX_LOG) {
        set_error(context, "rcs_checkin expects text content ending with newline and a text log");
        return;
    }
    memset(&archive, 0, sizeof(archive));
    delta.data = NULL;
    delta.length = 0;
    delta.capacity = 0;
    output.data = NULL;
    output.length = 0;
    output.capacity = 0;

    if (!is_empty && !parse_archive((const char *)archive_text,
                                    (size_t)archive_length, &archive)) {
        set_error(context, "rcs_checkin received an invalid or unsupported RCS archive");
        return;
    }
    old_count = archive.count;
    if (archive_revision_count(&archive) >= RCS_MAX_REVISIONS) {
        free_archive(&archive);
        set_error(context, "RCS archive has reached the revision limit");
        return;
    }
    if (old_count != 0) {
        Revision *old_head = &archive.revisions[old_count - 1];
        if (!make_reverse_delta((const char *)content, (size_t)content_length,
                                old_head->text, old_head->text_length, &delta)) {
            free_archive(&archive);
            sqlite3_free(delta.data);
            set_error(context, "out of memory while creating RCS delta");
            return;
        }
        sqlite3_free(old_head->text);
        old_head->text = delta.data;
        old_head->text_length = delta.length;
        delta.data = NULL;
    }
    {
        Revision *revisions = (Revision *)sqlite3_realloc64(
            archive.revisions,
            (sqlite3_uint64)(old_count + 1) * sizeof(Revision));
        if (revisions == NULL) {
            free_archive(&archive);
            set_error(context, "out of memory while adding RCS revision");
            return;
        }
        archive.revisions = revisions;
    }
    memset(&archive.revisions[old_count], 0, sizeof(Revision));
    archive.count = old_count + 1;
    new_revision = &archive.revisions[old_count];
    if (!make_date(date)) {
        free_archive(&archive);
        set_error(context, "failed to get the current UTC time");
        return;
    }
    new_revision->date = copy_bytes(date, strlen(date));
    new_revision->author = copy_bytes("sqlite", 6);
    new_revision->state = copy_bytes("Exp", 3);
    new_revision->log = copy_bytes((const char *)log, (size_t)log_length);
    new_revision->log_length = (size_t)log_length;
    new_revision->text = copy_bytes((const char *)content,
                                    (size_t)content_length);
    new_revision->text_length = (size_t)content_length;
    if (new_revision->date == NULL || new_revision->author == NULL ||
        new_revision->state == NULL || new_revision->log == NULL ||
        new_revision->text == NULL ||
        !serialize_archive(&archive, &output)) {
        free_archive(&archive);
        sqlite3_free(output.data);
        set_error(context, "failed to serialize RCS archive");
        return;
    }
    free_archive(&archive);
    sqlite3_result_text64(context, output.data, (sqlite3_uint64)output.length,
                          sqlite3_free, SQLITE_UTF8);
}

static int archive_revision_count(const Archive *archive)
{
    int total = archive->count;
    int i;
    for (i = 0; i < archive->branch_count; ++i) {
        if (archive->branches[i].count > RCS_MAX_REVISIONS - total) {
            return RCS_MAX_REVISIONS;
        }
        total += archive->branches[i].count;
    }
    return total;
}

static void create_branch_function(sqlite3_context *context, int argc,
                                   sqlite3_value **argv)
{
    Archive archive;
    Buffer output = {NULL, 0, 0};
    Buffer parent_content = {NULL, 0, 0};
    const unsigned char *archive_text;
    const unsigned char *name;
    int archive_length;
    int name_length;
    char *parent_revision;
    char *parent_branch_number = NULL;
    int parent_revision_number;
    int parent_branch_id;
    int parent_branch_revision;
    int next_branch_id = 1;
    int i;
    char *branch_number;

    (void)argc;
    archive_text = sqlite3_value_text(argv[0]);
    archive_length = sqlite3_value_bytes(argv[0]);
    name = sqlite3_value_text(argv[1]);
    name_length = sqlite3_value_bytes(argv[1]);
    if (archive_text == NULL || archive_length == 0 || name == NULL ||
        name_length > RCS_MAX_SYMBOL_NAME ||
        !valid_symbol_name((const char *)name, (size_t)name_length)) {
        set_error(context, "rcs_create_branch expects a non-empty archive and valid branch name");
        return;
    }
    if (!parse_archive((const char *)archive_text, (size_t)archive_length,
                       &archive)) {
        set_error(context, "rcs_create_branch received an invalid or unsupported RCS archive");
        return;
    }
    if (find_symbol(&archive, (const char *)name, (size_t)name_length) != NULL) {
        free_archive(&archive);
        set_error(context, "RCS symbol already exists");
        return;
    }
    if (!checkout_requested_revision(&archive, argv[2], &parent_content,
                                     &parent_revision)) {
        sqlite3_free(parent_content.data);
        free_archive(&archive);
        set_error(context, "branch parent must be an existing trunk or branch revision");
        return;
    }
    sqlite3_free(parent_content.data);
    if (!parse_trunk_revision_text(parent_revision, strlen(parent_revision),
                                   &parent_revision_number) &&
        !parse_branch_revision_path(parent_revision, strlen(parent_revision),
                                    &parent_branch_number,
                                    &parent_revision_number,
                                    &parent_branch_id,
                                    &parent_branch_revision)) {
        sqlite3_free(parent_revision);
        free_archive(&archive);
        set_error(context, "branch parent revision is invalid");
        return;
    }
    sqlite3_free(parent_branch_number);
    for (i = 0; i < archive.branch_count; ++i) {
        if (strcmp(archive.branches[i].parent_revision, parent_revision) == 0 &&
            archive.branches[i].branch_id >= next_branch_id) {
            if (archive.branches[i].branch_id == RCS_MAX_REVISIONS) {
                sqlite3_free(parent_revision);
                free_archive(&archive);
                set_error(context, "no branch numbers remain for this revision");
                return;
            }
            next_branch_id = archive.branches[i].branch_id + 1;
        }
    }
    branch_number = make_child_branch_number(parent_revision, next_branch_id);
    if (branch_number == NULL ||
        add_branch(&archive, branch_number, parent_revision,
                   parent_revision_number, next_branch_id) == NULL ||
        !set_symbol(&archive, (const char *)name, (size_t)name_length,
                    branch_number, 1) ||
        !serialize_archive(&archive, &output)) {
        sqlite3_free(branch_number);
        sqlite3_free(parent_revision);
        free_archive(&archive);
        sqlite3_free(output.data);
        set_error(context, "failed to create RCS branch");
        return;
    }
    sqlite3_free(branch_number);
    sqlite3_free(parent_revision);
    free_archive(&archive);
    sqlite3_result_text64(context, output.data, (sqlite3_uint64)output.length,
                          sqlite3_free, SQLITE_UTF8);
}

static void branch_checkin_function(sqlite3_context *context, int argc,
                                   sqlite3_value **argv)
{
    Archive archive;
    Buffer output = {NULL, 0, 0};
    const unsigned char *archive_text;
    const unsigned char *name;
    const unsigned char *content;
    const unsigned char *log;
    int archive_length;
    int name_length;
    int content_length;
    int log_length;
    int old_count;
    int total;
    char date[32];
    Symbol *symbol;
    Branch *branch;
    Revision *new_revision;

    (void)argc;
    archive_text = sqlite3_value_text(argv[0]);
    archive_length = sqlite3_value_bytes(argv[0]);
    name = sqlite3_value_text(argv[1]);
    name_length = sqlite3_value_bytes(argv[1]);
    content = sqlite3_value_text(argv[2]);
    content_length = sqlite3_value_bytes(argv[2]);
    log = sqlite3_value_text(argv[3]);
    log_length = sqlite3_value_bytes(argv[3]);
    if (archive_text == NULL || archive_length == 0 || name == NULL ||
        content == NULL || log == NULL ||
        !valid_text_input(content, content_length) ||
        (log_length != 0 &&
         (memchr(log, '\0', (size_t)log_length) != NULL ||
          !valid_utf8((const char *)log, (size_t)log_length))) ||
        content_length > RCS_MAX_CONTENT || log_length > RCS_MAX_LOG) {
        set_error(context, "rcs_branch_checkin expects an archive, branch name, newline-terminated content, and text log");
        return;
    }
    if (!parse_archive((const char *)archive_text, (size_t)archive_length,
                       &archive)) {
        set_error(context, "rcs_branch_checkin received an invalid or unsupported RCS archive");
        return;
    }
    symbol = find_symbol(&archive, (const char *)name, (size_t)name_length);
    if (symbol == NULL || !symbol->is_branch) {
        free_archive(&archive);
        set_error(context, "RCS branch does not exist; create it with rcs_create_branch");
        return;
    }
    branch = find_branch(&archive, symbol->revision);
    if (branch == NULL) {
        free_archive(&archive);
        set_error(context, "RCS branch symbol does not reference a known branch");
        return;
    }
    total = archive_revision_count(&archive);
    if (total >= RCS_MAX_REVISIONS || branch->count >= RCS_MAX_REVISIONS) {
        free_archive(&archive);
        set_error(context, "RCS archive has reached the revision limit");
        return;
    }
    old_count = branch->count;
    if (!reserve_branch_revisions(branch, old_count + 1)) {
        free_archive(&archive);
        set_error(context, "out of memory while adding branch revision");
        return;
    }
    if (!make_date(date)) {
        free_archive(&archive);
        set_error(context, "failed to get the current UTC time");
        return;
    }
    branch->count = old_count + 1;
    new_revision = &branch->revisions[old_count];
    new_revision->date = copy_bytes(date, strlen(date));
    new_revision->author = copy_bytes("sqlite", 6);
    new_revision->state = copy_bytes("Exp", 3);
    new_revision->log = copy_bytes((const char *)log, (size_t)log_length);
    new_revision->log_length = (size_t)log_length;
    new_revision->text = copy_bytes((const char *)content,
                                    (size_t)content_length);
    new_revision->text_length = (size_t)content_length;
    if (new_revision->date == NULL || new_revision->author == NULL ||
        new_revision->state == NULL || new_revision->log == NULL ||
        new_revision->text == NULL ||
        !serialize_archive(&archive, &output)) {
        free_archive(&archive);
        sqlite3_free(output.data);
        set_error(context, "failed to serialize RCS branch revision");
        return;
    }
    free_archive(&archive);
    sqlite3_result_text64(context, output.data, (sqlite3_uint64)output.length,
                          sqlite3_free, SQLITE_UTF8);
}

static void checkout_function(sqlite3_context *context, int argc,
                              sqlite3_value **argv)
{
    Archive archive;
    Buffer content;
    const unsigned char *archive_text;
    int archive_length;
    char *revision;

    (void)argc;
    archive_text = sqlite3_value_text(argv[0]);
    archive_length = sqlite3_value_bytes(argv[0]);
    if (archive_text == NULL) {
        set_error(context, "rcs_checkout expects an archive and revision");
        return;
    }
    if (!parse_archive((const char *)archive_text, (size_t)archive_length,
                       &archive)) {
        set_error(context, "rcs_checkout received an invalid or unsupported RCS archive");
        return;
    }
    if (!checkout_requested_revision(&archive, argv[1], &content,
                                     &revision)) {
        free_archive(&archive);
        set_error(context, "RCS revision or symbol does not exist, or its delta is invalid");
        return;
    }
    sqlite3_free(revision);
    free_archive(&archive);
    sqlite3_result_text64(context, content.data, (sqlite3_uint64)content.length,
                          sqlite3_free, SQLITE_UTF8);
}

static void diff_function(sqlite3_context *context, int argc,
                          sqlite3_value **argv)
{
    Archive archive;
    Buffer left;
    Buffer right;
    Buffer output;
    const unsigned char *archive_text;
    int archive_length;
    char *left_revision;
    char *right_revision;
    int status;

    (void)argc;
    archive_text = sqlite3_value_text(argv[0]);
    archive_length = sqlite3_value_bytes(argv[0]);
    if (archive_text == NULL) {
        set_error(context, "rcs_diff expects an archive and revisions");
        return;
    }
    if (!parse_archive((const char *)archive_text, (size_t)archive_length,
                       &archive)) {
        set_error(context, "rcs_diff received an invalid or unsupported RCS archive");
        return;
    }
    if (!checkout_requested_revision(&archive, argv[1], &left,
                                     &left_revision)) {
        free_archive(&archive);
        set_error(context, "RCS revision or symbol does not exist");
        return;
    }
    if (!checkout_requested_revision(&archive, argv[2], &right,
                                     &right_revision)) {
        sqlite3_free(left.data);
        sqlite3_free(left_revision);
        free_archive(&archive);
        set_error(context, "RCS revision or symbol does not exist");
        return;
    }
    output.data = NULL;
    output.length = 0;
    output.capacity = 0;
    status = make_unified_diff(left.data, left.length, left_revision,
                               right.data, right.length, right_revision,
                               &output);
    sqlite3_free(left.data);
    sqlite3_free(right.data);
    sqlite3_free(left_revision);
    sqlite3_free(right_revision);
    free_archive(&archive);
    if (status != 1) {
        sqlite3_free(output.data);
        set_error(context, status < 0
            ? "rcs_diff comparison exceeds 4,000,000 dynamic-programming cells"
            : "failed to compute RCS diff");
        return;
    }
    if (output.data == NULL) {
        output.data = copy_bytes("", 0);
        if (output.data == NULL) {
            set_error(context, "out of memory while creating RCS diff");
            return;
        }
    }
    sqlite3_result_text64(context, output.data,
                          (sqlite3_uint64)output.length,
                          sqlite3_free, SQLITE_UTF8);
}

static void merge_function(sqlite3_context *context, int argc,
                           sqlite3_value **argv)
{
    Archive archive;
    Buffer base_content = {NULL, 0, 0};
    Buffer ours_content = {NULL, 0, 0};
    Buffer theirs_content = {NULL, 0, 0};
    Buffer merged = {NULL, 0, 0};
    Buffer result = {NULL, 0, 0};
    const unsigned char *archive_text;
    int archive_length;
    char *revision = NULL;
    int has_conflicts;
    int status;

    (void)argc;
    archive_text = sqlite3_value_text(argv[0]);
    archive_length = sqlite3_value_bytes(argv[0]);
    if (archive_text == NULL ||
        !parse_archive((const char *)archive_text, (size_t)archive_length,
                       &archive)) {
        set_error(context, "rcs_merge received an invalid or unsupported RCS archive");
        return;
    }
    if (!checkout_requested_revision(&archive, argv[1], &base_content,
                                     &revision)) {
        free_archive(&archive);
        set_error(context, "rcs_merge base revision does not exist");
        return;
    }
    sqlite3_free(revision);
    revision = NULL;
    if (!checkout_requested_revision(&archive, argv[2], &ours_content,
                                     &revision)) {
        sqlite3_free(base_content.data);
        free_archive(&archive);
        set_error(context, "rcs_merge ours revision does not exist");
        return;
    }
    sqlite3_free(revision);
    revision = NULL;
    if (!checkout_requested_revision(&archive, argv[3], &theirs_content,
                                     &revision)) {
        sqlite3_free(base_content.data);
        sqlite3_free(ours_content.data);
        free_archive(&archive);
        set_error(context, "rcs_merge theirs revision does not exist");
        return;
    }
    sqlite3_free(revision);
    status = merge_contents(base_content.data, base_content.length,
                            ours_content.data, ours_content.length,
                            theirs_content.data, theirs_content.length,
                            &merged, &has_conflicts);
    sqlite3_free(base_content.data);
    sqlite3_free(ours_content.data);
    sqlite3_free(theirs_content.data);
    free_archive(&archive);
    if (status != 1) {
        set_error(context, status == -1
            ? "rcs_merge comparison exceeds 4,000,000 dynamic-programming cells"
            : status == -2
                ? "rcs_merge result exceeds the 8 MiB content limit"
                : "failed to compute RCS merge");
        return;
    }
    if (!buffer_append_cstr(&result, "{\"content\":") ||
        !append_json_string(&result, merged.data, merged.length) ||
        !buffer_append_cstr(&result, ",\"has_conflicts\":") ||
        !buffer_append_cstr(&result, has_conflicts ? "true}" : "false}") ||
        result.length > RCS_MAX_ARCHIVE) {
        sqlite3_free(merged.data);
        sqlite3_free(result.data);
        set_error(context, "failed to format RCS merge result");
        return;
    }
    sqlite3_free(merged.data);
    sqlite3_result_text64(context, result.data,
                          (sqlite3_uint64)result.length,
                          sqlite3_free, SQLITE_UTF8);
}

#if defined(_WIN32)
__declspec(dllexport)
#endif
int sqlite3_sqlitercs_init(sqlite3 *database, char **error_message,
                           const sqlite3_api_routines *api)
{
    int result;
    SQLITE_EXTENSION_INIT2(api);
    (void)error_message;
    result = sqlite3_create_function(database, "rcs_checkin", 3,
                                     SQLITE_UTF8, NULL, checkin_function,
                                     NULL, NULL);
    if (result != SQLITE_OK) {
        return result;
    }
    result = sqlite3_create_function(database, "rcs_create_branch", 3,
                                     SQLITE_UTF8, NULL,
                                     create_branch_function, NULL, NULL);
    if (result != SQLITE_OK) {
        return result;
    }
    result = sqlite3_create_function(database, "rcs_branch_checkin", 4,
                                     SQLITE_UTF8, NULL,
                                     branch_checkin_function, NULL, NULL);
    if (result != SQLITE_OK) {
        return result;
    }
    result = sqlite3_create_function(database, "rcs_checkout", 2,
                                     SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                     NULL, checkout_function, NULL, NULL);
    if (result != SQLITE_OK) {
        return result;
    }
    result = sqlite3_create_function(database, "rcs_head", 1,
                                     SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                     NULL, head_function, NULL, NULL);
    if (result != SQLITE_OK) {
        return result;
    }
    result = sqlite3_create_function(database, "rcs_revisions", 1,
                                     SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                     NULL, revisions_function, NULL, NULL);
    if (result != SQLITE_OK) {
        return result;
    }
    result = sqlite3_create_function(database, "rcs_tag", 3,
                                     SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                     NULL, tag_function, NULL, NULL);
    if (result != SQLITE_OK) {
        return result;
    }
    result = sqlite3_create_function(database, "rcs_symbols", 1,
                                     SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                     NULL, symbols_function, NULL, NULL);
    if (result != SQLITE_OK) {
        return result;
    }
    result = sqlite3_create_function(database, "rcs_resolve_revision", 2,
                                     SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                     NULL, resolve_function, NULL, NULL);
    if (result != SQLITE_OK) {
        return result;
    }
    result = sqlite3_create_function(database, "rcs_is_valid", 1,
                                     SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                     NULL, is_valid_function, NULL, NULL);
    if (result != SQLITE_OK) {
        return result;
    }
    result = sqlite3_create_function(database, "rcs_import", 1,
                                     SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                     NULL, import_function, NULL, NULL);
    if (result != SQLITE_OK) {
        return result;
    }
    result = sqlite3_create_function(database, "rcs_export", 1,
                                     SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                     NULL, export_function, NULL, NULL);
    if (result != SQLITE_OK) {
        return result;
    }
    result = sqlite3_create_function(database, "rcs_diff", 3,
                                     SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                     NULL, diff_function, NULL, NULL);
    if (result != SQLITE_OK) {
        return result;
    }
    return sqlite3_create_function(database, "rcs_merge", 4,
                                   SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                   NULL, merge_function, NULL, NULL);
}
