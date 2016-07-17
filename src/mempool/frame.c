#include "../_ft_internal.h"

void _frame_init(struct frame * this, uint8_t * data, size_t capacity, struct ft_poolzone * zone)
{
	assert(this != NULL);
	assert(((long)data % MEMPAGE_SIZE) == 0);

	this->type = FT_FRAME_TYPE_FREE;
	this->zone = zone;
	this->data = data;
	this->capacity = capacity;

	this->borrowed_by_file = NULL;
	this->borrowed_by_line = 0;

	this->vec_position = 0;
	this->vec_limit = 0;

	bzero(this->data, FRAME_SIZE);
}

size_t frame_total_start_to_position(struct frame * this)
{
	assert(this != NULL);

	size_t length = 0;
	struct ft_vec * vecs = (struct ft_vec *)(this->data + this->capacity);
	for (int i=-1; i>(-1-this->vec_limit); i -= 1)
	{
		length += vecs[i].position;
	}

	return length;
}

size_t frame_total_position_to_limit(struct frame * this)
{
	assert(this != NULL);

	size_t length = 0;
	struct ft_vec * vecs = (struct ft_vec *)(this->data + this->capacity);
	for (int i=-1; i>(-1-this->vec_limit); i -= 1)
	{
		length += vecs[i].limit - vecs[i].position;
	}

	return length;
}

struct ft_vec * frame_add_dvec(struct frame * this, size_t offset, size_t capacity)
{
	assert(this != NULL);	
	assert(offset >= 0);
	assert(capacity >= 0);

	struct ft_vec * vec = (struct ft_vec *)(this->data + this->capacity);
	vec -= this->vec_limit + 1;

	//Test if there is enough space in the frame
	if (((uint8_t *)vec - this->data) < (offset + capacity))
	{
		FT_ERROR("Cannot accomodate that vec in the current frame.");
		return NULL;
	}


	this->vec_limit += 1;

	assert((uint8_t *)vec  > this->data);
	assert((void *)vec + this->vec_limit * sizeof(struct ft_vec) == (this->data + this->capacity));

	vec->frame = this;
	vec->offset = offset;
	vec->position = 0;
	vec->limit = vec->capacity = capacity;

	return vec;
}

void frame_format_empty(struct frame * this)
{
	assert(this != NULL);

	this->vec_limit = 0;
	this->vec_position = 0;	
}

void frame_format_simple(struct frame * this)
{
	frame_format_empty(this);
	frame_add_dvec(this, 0, this->capacity - sizeof(struct ft_vec));
}


void frame_print(struct frame * this)
{
	struct ft_vec * vec = (struct ft_vec *)(this->data + this->capacity);
	for (int i=-1; i>(-1-this->vec_limit); i -= 1)
		write(STDOUT_FILENO, vec[i].frame->data + vec[i].offset, vec[i].limit);
}


/// ft_vec methods follows ...

bool ft_vec_vsprintf(struct ft_vec * this, const char * format, va_list args)
{
	size_t max_size = (this->limit - this->position) - 1;
	int rc = vsnprintf((char *)ft_vec_ptr(this), max_size, format, args);

	if (rc < 0) return false;
	if (rc > max_size) return false;
	this->position += rc;	

	return true;
}

bool ft_vec_sprintf(struct ft_vec * this, const char * format, ...)
{
	va_list ap;

	va_start(ap, format);
	bool ok = ft_vec_vsprintf(this, format, ap);
	va_end(ap);

	return ok;
}

bool ft_vec_cat(struct ft_vec * this, const void * data, size_t data_len)
{
	if ((this->position + data_len) > this->limit) return false;

	memcpy(ft_vec_ptr(this), data, data_len);
	this->position += data_len;

	return true;
}

bool ft_vec_strcat(struct ft_vec * this, const char * text)
{
	return ft_vec_cat(this, text, strlen(text));
}