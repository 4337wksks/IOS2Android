struct TableState;
struct TableRegistration { Word identifier,cls;struct TableRegistration *next; };
struct TableCellState {
    Word object,identifier,content,label,detail,image,accessory,background,selected_background;
    long style,selection_style,accessory_type,indentation;
    double indentation_width;
    struct GuestInsets separator;
    unsigned selected,highlighted,epoch,cached,separator_set;
    Method prepare,layout;
    struct TableState *table;
};
struct TableSection {
    long first,rows;
    Word header_title,footer_title,header_view,footer_view;
    double origin,header_height,footer_height;
    unsigned header,footer;
};
struct TableRow {
    Word object;
    long position,section,index;
    unsigned visible,displayed,queued,cell,kind;
    struct TableRow *next;
};
struct TableHeight { long position;double delta,prefix; };
struct TableState {
    Word object,source,delegate,header,footer,background,separator_color,selected;
    long style,separator_style,section_count,total;
    double row_height,estimated_height,header_height,footer_height,base_height;
    struct GuestInsets separator;
    unsigned selection,multiple,readable,dirty,updates,generation,synced,closed,epoch,cached,dragging,decelerating,animated,refreshing,binding;
    Word synced_owner;
    Method source_methods[5],delegate_methods[19];
    struct TableSection *sections;
    struct TableRow *rows;
    struct TableHeight *heights;
    struct TableRegistration *registrations;
    size_t height_count,height_capacity;
    float metrics[18];int options[14];
};
static void table_recycle(struct TableState *,Word);
static void table_row_display(struct TableState *,struct TableRow *,unsigned);
static const char *const table_source_callbacks[]={"numberOfSectionsInTableView:","tableView:numberOfRowsInSection:","tableView:cellForRowAtIndexPath:","tableView:titleForHeaderInSection:","tableView:titleForFooterInSection:"};
static const char *const table_delegate_callbacks[]={"tableView:heightForRowAtIndexPath:","tableView:heightForHeaderInSection:","tableView:heightForFooterInSection:","tableView:viewForHeaderInSection:","tableView:viewForFooterInSection:","tableView:willDisplayCell:forRowAtIndexPath:","tableView:didEndDisplayingCell:forRowAtIndexPath:","tableView:willSelectRowAtIndexPath:","tableView:didSelectRowAtIndexPath:","tableView:willDeselectRowAtIndexPath:","tableView:didDeselectRowAtIndexPath:","tableView:accessoryButtonTappedForRowWithIndexPath:","scrollViewWillBeginDragging:","scrollViewDidEndDragging:willDecelerate:","scrollViewWillBeginDecelerating:","scrollViewDidEndDecelerating:","scrollViewDidEndScrollingAnimation:","tableView:willDisplayHeaderView:forSection:","tableView:willDisplayFooterView:forSection:"};
static struct TableState *table_state(Word object) {
    struct ViewState *view=view_state(object);
    if(!view->table){struct TableState *table=calloc(1,sizeof(*table));if(!table)stop("Allocation failed","UITableView");table->object=object;table->row_height=44;table->estimated_height=44;table->header_height=table->footer_height=-1;table->selection=table->readable=1;table->separator_style=1;table->separator=(struct GuestInsets){0,16,0,0};table->dirty=1;table->selected=sequence_empty(external_class("_OBJC_CLASS_$_NSMutableArray"));view->table=table;view->clips=1;scroll_state(object);}
    return view->table;
}
static struct TableCellState *table_cell_state(Word object) {
    struct ViewState *view=view_state(object);
    if(!view->cell){struct TableCellState *cell=calloc(1,sizeof(*cell));if(!cell)stop("Allocation failed","UITableViewCell");cell->object=object;cell->selection_style=3;cell->indentation_width=10;view->cell=cell;}
    return view->cell;
}
static void table_callbacks(struct TableState *table) {
    unsigned epoch=__atomic_load_n(&method_cache_epoch,__ATOMIC_ACQUIRE);
    if(table->cached && table->epoch==epoch)return;
    for(unsigned i=0;i<COUNT(table->source_methods);++i)table->source_methods[i]=table->source ? find_implementation(table->source,table_source_callbacks[i]) : NULL;
    for(unsigned i=0;i<COUNT(table->delegate_methods);++i)table->delegate_methods[i]=table->delegate ? find_implementation(table->delegate,table_delegate_callbacks[i]) : NULL;
    table->epoch=epoch;table->cached=1;
}
static Word table_path(long section,long row) { return index_path_create(external_class("_OBJC_CLASS_$_NSIndexPath"),"indexPathForRow:inSection:",(Word)row,(Word)section); }
static long table_path_component(Word path,unsigned component,const char *selector) {
    if(!path)stop("A row index path is required",selector);
    const char *name=component ? "row" : "section";Method method=find_implementation(path,name);
    if(!method)stop("Invalid row index path",selector);return (long)method(path,name,0,0,0,0);
}
static double table_default_height(struct TableState *table) { return table->row_height>0 ? table->row_height : table->estimated_height>0 ? table->estimated_height : 44; }
static void table_command(struct TableState *table,int action,int position,float offset,int animated,int mode) {
    if(table->closed)return;JNIEnv *env=attached_environment("Table command");static jmethodID method;
    if(!method){jclass cls=(*env)->GetObjectClass(env,activity);method=(*env)->GetMethodID(env,cls,"commandGuestTable","(JIIFZI)V");(*env)->DeleteLocalRef(env,cls);}
    (*env)->CallVoidMethod(env,activity,method,(jlong)table->object,action,position,offset,(jboolean)animated,mode);
    if((*env)->ExceptionCheck(env)){(*env)->ExceptionClear(env);stop("Android table command failed","UITableView");}
}
static void table_free_sections(struct TableState *table) {
    for(long i=0;i<table->section_count;++i){struct TableSection *section=&table->sections[i];metal_object_release(section->header_title);metal_object_release(section->footer_title);if(section->header_view)view_remove(section->header_view);if(section->footer_view)view_remove(section->footer_view);metal_object_release(section->header_view);metal_object_release(section->footer_view);}
    free(table->sections);table->sections=NULL;table->section_count=0;table->height_count=0;
}
static void table_refresh(struct TableState *table) {
    if(!table->dirty || table->updates || table->refreshing || table->binding)return;table->refreshing=1;table->dirty=0;table_callbacks(table);
    for(struct TableRow *row=table->rows,*next;row;row=next){next=row->next;if(row->visible)table_recycle(table,row->object);}
    table_free_sections(table);
    long count=table->source ? table->source_methods[0] ? (long)table->source_methods[0](table->source,table_source_callbacks[0],table->object,0,0,0) : 1 : 0;
    if(count<0 || count>INT_MAX || (size_t)count>SIZE_MAX/sizeof(*table->sections))stop("Invalid table section count","numberOfSectionsInTableView:");
    if(count){table->sections=calloc((size_t)count,sizeof(*table->sections));if(!table->sections)stop("Allocation failed","Table sections");}
    table->section_count=count;table->total=table->header && view_state(table->header)->frame.height>0 ? 1 : 0;double y=table->total ? view_state(table->header)->frame.height : 0;
    if(count && !table->source_methods[1])stop("Missing table data source method",table_source_callbacks[1]);
    for(long i=0;i<count;++i){struct TableSection *section=&table->sections[i];section->rows=(long)table->source_methods[1](table->source,table_source_callbacks[1],table->object,(Word)i,0,0);
        if(table->total>INT_MAX-2 || section->rows<0 || section->rows>INT_MAX-table->total-2)stop("Invalid table row count",table_source_callbacks[1]);
        if(table->source_methods[3])section->header_title=metal_object_retain(table->source_methods[3](table->source,table_source_callbacks[3],table->object,(Word)i,0,0));
        if(table->source_methods[4])section->footer_title=metal_object_retain(table->source_methods[4](table->source,table_source_callbacks[4],table->object,(Word)i,0,0));
        section->header_height=table->header_height>=0 ? table->header_height : 22;section->footer_height=table->footer_height>=0 ? table->footer_height : 22;
        if(table->delegate_methods[1])section->header_height=((double (*)(Word,const char *,Word,long))table->delegate_methods[1])(table->delegate,table_delegate_callbacks[1],table->object,i);
        if(table->delegate_methods[2])section->footer_height=((double (*)(Word,const char *,Word,long))table->delegate_methods[2])(table->delegate,table_delegate_callbacks[2],table->object,i);
        if(!isfinite(section->header_height) || !isfinite(section->footer_height))stop("Invalid table section height","UITableView");
        if(section->header_height==-1)section->header_height=22;if(section->footer_height==-1)section->footer_height=22;
        section->header_height=fmax(0,section->header_height);section->footer_height=fmax(0,section->footer_height);
        section->header=section->header_height>0 && (section->header_title || table->delegate_methods[3]);section->footer=section->footer_height>0 && (section->footer_title || table->delegate_methods[4]);
        section->first=table->total;section->origin=y;table->total+=section->rows+section->header+section->footer;y+=section->rows*table_default_height(table)+(section->header ? section->header_height : 0)+(section->footer ? section->footer_height : 0);
    }
    if(table->footer && view_state(table->footer)->frame.height>0){if(table->total==INT_MAX)stop("Table row count exceeds Android capacity","UITableView");++table->total;y+=view_state(table->footer)->frame.height;}
    if(!isfinite(y))stop("Table content height exceeds supported range","UITableView");table->base_height=y;scroll_state(table->object)->size=(struct GuestSize){view_state(table->object)->bounds.width,y};table->refreshing=0;++table->generation;
}
static size_t table_height_index(struct TableState *table,long position) {
    size_t low=0,high=table->height_count;while(low<high){size_t middle=low+(high-low)/2;if(table->heights[middle].position<position)low=middle+1;else high=middle;}return low;
}
static double table_height_prefix(struct TableState *table,long position) { size_t index=table_height_index(table,position);return index ? table->heights[index-1].prefix : 0; }
static void table_measure(struct TableState *table,long position,double height,double estimate) {
    double delta=height-estimate;size_t index=table_height_index(table,position);
    if(index<table->height_count && table->heights[index].position==position){if(fabs(table->heights[index].delta-delta)<0.001)return;table->heights[index].delta=delta;}
    else{if(fabs(delta)<0.001)return;if(table->height_count==table->height_capacity){size_t capacity=table->height_capacity ? table->height_capacity*2 : 16;struct TableHeight *entries=realloc(table->heights,capacity*sizeof(*entries));if(!entries)stop("Allocation failed","Table measured heights");table->heights=entries;table->height_capacity=capacity;}
        memmove(table->heights+index+1,table->heights+index,(table->height_count-index)*sizeof(*table->heights));table->heights[index]=(struct TableHeight){position,delta,0};++table->height_count;}
    double prefix=index ? table->heights[index-1].prefix : 0;for(size_t i=index;i<table->height_count;++i){prefix+=table->heights[i].delta;table->heights[i].prefix=prefix;}
    scroll_state(table->object)->size.height=table->base_height+prefix;
}
struct TablePosition { long section,row;unsigned kind;double height,origin; };
static struct TablePosition table_position(struct TableState *table,long position) {
    table_refresh(table);if(position<0 || position>=table->total)stop("Table position out of range","UITableView");
    if(table->header && view_state(table->header)->frame.height>0 && position==0)return (struct TablePosition){0,0,3,view_state(table->header)->frame.height,0};
    long low=0,high=table->section_count;while(low<high){long middle=low+(high-low)/2;if(table->sections[middle].first<=position)low=middle+1;else high=middle;}
    if(low){long index=low-1;struct TableSection *section=&table->sections[index];long row=position-section->first;double origin=section->origin+table_height_prefix(table,position);
        if(section->header && !row)return (struct TablePosition){index,0,1,section->header_height,origin};row-=section->header;
        if(row<section->rows)return (struct TablePosition){index,row,0,table_default_height(table),origin+(section->header ? section->header_height : 0)+row*table_default_height(table)};
        if(section->footer && row==section->rows)return (struct TablePosition){index,0,2,section->footer_height,origin+(section->header ? section->header_height : 0)+section->rows*table_default_height(table)};
    }
    return (struct TablePosition){0,0,4,view_state(table->footer)->frame.height,table->base_height-view_state(table->footer)->frame.height+table_height_prefix(table,position)};
}
static long table_position_for_path(struct TableState *table,Word path,const char *selector) {
    table_refresh(table);long section=table_path_component(path,0,selector),row=table_path_component(path,1,selector);
    if(section<0 || section>=table->section_count || row<0 || row>=table->sections[section].rows)stop("Table row index path out of range",selector);return table->sections[section].first+table->sections[section].header+row;
}
static struct TableRow *table_row_object(struct TableState *table,Word object) { for(struct TableRow *row=table->rows;row;row=row->next)if(row->object==object)return row;return NULL; }
static int table_selected(struct TableState *table,long section,long row) {
    struct Sequence *paths=(void *)table->selected;for(size_t i=0;i<paths->count;++i)if(table_path_component(paths->items[i],0,"Selection")==section && table_path_component(paths->items[i],1,"Selection")==row)return 1;return 0;
}
static void table_cell_layout(Word,const char *);
static void table_cell_appearance(struct TableCellState *cell) {
    if(!cell->table || !view_state(cell->object)->host_container)return;
    JNIEnv *env=attached_environment("Table cell appearance");static jmethodID method;
    if(!method){jclass cls=(*env)->GetObjectClass(env,activity);method=(*env)->GetMethodID(env,cls,"syncGuestTableRow","(JJ[F[I)V");(*env)->DeleteLocalRef(env,cls);}
    float metrics[]={(float)(cell->separator.left*screen_scale()),(float)(cell->separator.right*screen_scale())};int options[]={cell->accessory ? 0 : (int)cell->accessory_type,(int)cell->selection_style,cell->selected,cell->separator_set};
    jfloatArray m=(*env)->NewFloatArray(env,COUNT(metrics));jintArray o=(*env)->NewIntArray(env,COUNT(options));(*env)->SetFloatArrayRegion(env,m,0,COUNT(metrics),metrics);(*env)->SetIntArrayRegion(env,o,0,COUNT(options),options);(*env)->CallVoidMethod(env,activity,method,(jlong)cell->table->object,(jlong)cell->object,m,o);(*env)->DeleteLocalRef(env,m);(*env)->DeleteLocalRef(env,o);
    if((*env)->ExceptionCheck(env)){(*env)->ExceptionClear(env);stop("Android table cell appearance failed","UITableViewCell");}
}
static void table_cell_set_selection(Word object,const char *selector,Word value,Word animated) {
    struct TableCellState *cell=table_cell_state(object);if(!strncmp(selector,"setHighlighted:",15))cell->highlighted=value!=0;else cell->selected=value!=0;
    if(cell->background)view_state(cell->background)->hidden=cell->selected && cell->selected_background!=0;
    if(cell->selected_background)view_state(cell->selected_background)->hidden=!cell->selected;
    view_sync_subtree(object);table_cell_appearance(cell);
}
static uint32_t ui_cell_background(Word object,uint32_t color) {
    struct TableCellState *cell=view_state(object)->cell;if(!cell || !cell->selection_style || cell->selected_background || (!cell->selected && !cell->highlighted))return color;
    return cell->selection_style==1 ? 0xff007aff : 0xffd9d9de;
}
static Word table_cell_content(Word object,const char *selector) {
    struct TableCellState *cell=table_cell_state(object);if(!cell->content){cell->content=view_init_frame(host_allocate(external_class("_OBJC_CLASS_$_UIView")),selector,view_state(object)->bounds);view_add(object,selector,cell->content);}return cell->content;
}
static Word table_cell_label(Word object,const char *selector) {
    struct TableCellState *cell=table_cell_state(object);if(!strcmp(selector,"detailTextLabel") && cell->style==0)return 0;Word *slot=!strcmp(selector,"detailTextLabel") ? &cell->detail : &cell->label;
    if(!*slot){*slot=view_init_frame(host_allocate(external_class("_OBJC_CLASS_$_UILabel")),selector,(struct GuestRect){0});label_state(*slot)->font=ui_font_system(external_class("_OBJC_CLASS_$_UIFont"),"systemFontOfSize:",slot==&cell->detail ? 14 : 17);view_add(table_cell_content(object,selector),selector,*slot);table_cell_layout(object,"layoutSubviews");}return *slot;
}
static Word table_cell_image(Word object,const char *selector) {
    struct TableCellState *cell=table_cell_state(object);if(!cell->image){cell->image=host_allocate(external_class("_OBJC_CLASS_$_UIImageView"));view_init_frame(cell->image,selector,(struct GuestRect){0});control_state(cell->image);view_state(cell->image)->content_mode=1;view_add(table_cell_content(object,selector),selector,cell->image);}table_cell_layout(object,"layoutSubviews");return cell->image;
}
static void table_cell_frame(Word object,struct GuestRect frame) { struct ViewState *view=view_state(object);view->frame=frame;view->bounds=(struct GuestRect){0,0,frame.width,frame.height}; }
static void table_cell_layout(Word object,const char *selector) {
    struct TableCellState *cell=table_cell_state(object);struct ViewState *view=view_state(object);double width=view->bounds.width,height=view->bounds.height,left=view->margins.y+cell->indentation*cell->indentation_width,right=view->margins.height;
    if(cell->table && cell->table->readable){double readable=40*17.0;double margin=fmax(0,(width-readable)*0.5);left=fmax(left,margin);right=fmax(right,margin);}
    if(cell->background){view_state(cell->background)->hidden=cell->selected && cell->selected_background!=0;table_cell_frame(cell->background,(struct GuestRect){0,0,width,height});}if(cell->selected_background){view_state(cell->selected_background)->hidden=!cell->selected;table_cell_frame(cell->selected_background,(struct GuestRect){0,0,width,height});}
    if(cell->accessory){struct GuestSize size=view_intrinsic_for_layout(cell->accessory);struct GuestRect frame=view_state(cell->accessory)->frame;double w=size.width>0 ? size.width : frame.width,h=size.height>0 ? size.height : frame.height;table_cell_frame(cell->accessory,(struct GuestRect){fmax(0,width-right-w),fmax(0,(height-h)*0.5),w,h});right+=w+12;}
    else if(cell->accessory_type)right+=28;
    Word content=table_cell_content(object,selector);table_cell_frame(content,(struct GuestRect){0,0,width,height});
    if(cell->image){struct ControlState *image=control_state(cell->image);struct ImageSize natural=image->image ? ui_image_size(image->image) : (struct ImageSize){0};double edge=natural.width>0 ? fmin(40,fmax(0,height-12)) : 0;table_cell_frame(cell->image,(struct GuestRect){left,(height-edge)*0.5,edge,edge});if(edge)left+=edge+12;}
    double text_width=fmax(0,width-left-right);
    if(cell->label){double label_height=cell->style==3 && cell->detail ? 22 : height;table_cell_frame(cell->label,(struct GuestRect){left,cell->style==3 && cell->detail ? fmax(0,height*0.5-22) : 0,text_width,label_height});}
    if(cell->detail){if(cell->style==3)table_cell_frame(cell->detail,(struct GuestRect){left,height*0.5,text_width,fmax(0,height*0.5-2)});
        else{double detail_width=label_size(cell->detail,"sizeThatFits:",(struct GuestSize){text_width,height}).width;detail_width=fmin(text_width*0.6,detail_width);table_cell_frame(cell->detail,(struct GuestRect){width-right-detail_width,0,detail_width,height});if(cell->label)view_state(cell->label)->frame.width=fmax(0,text_width-detail_width-8);}}
    view_sync_subtree(object);
    table_cell_appearance(cell);
}
static Word table_cell_init(Word object,const char *selector,long style,Word identifier) {
    if(style<0 || style>3)stop("Invalid table cell style",selector);struct TableCellState *cell=table_cell_state(object);cell->style=style;cell->identifier=identifier ? text_copy(identifier,"copy") : 0;view_init_frame(object,selector,(struct GuestRect){0,0,0,44});view_state(object)->margins=(struct GuestRect){0,16,0,16};table_cell_content(object,selector);return object;
}
static Word table_cell_init_plain(Word object,const char *selector) { return table_cell_init(object,selector,0,0); }
static Word table_cell_init_frame(Word object,const char *selector,struct GuestRect frame) { table_cell_init_plain(object,selector);view_init_frame(object,selector,frame);table_cell_layout(object,"layoutSubviews");return object; }
static Word table_cell_property(Word object,const char *selector) {
    struct TableCellState *cell=table_cell_state(object);
    if(!strcmp(selector,"reuseIdentifier"))return cell->identifier;if(!strcmp(selector,"selectionStyle"))return cell->selection_style;if(!strcmp(selector,"accessoryType"))return cell->accessory_type;if(!strcmp(selector,"accessoryView"))return cell->accessory;
    if(!strcmp(selector,"backgroundView"))return cell->background;if(!strcmp(selector,"selectedBackgroundView"))return cell->selected_background;if(!strcmp(selector,"isSelected"))return cell->selected;if(!strcmp(selector,"isHighlighted"))return cell->highlighted;return cell->indentation;
}
static void table_cell_set_property(Word object,const char *selector,Word value) {
    struct TableCellState *cell=table_cell_state(object);
    if(!strcmp(selector,"setSelectionStyle:")){if(value>3)stop("Invalid table cell selection style",selector);cell->selection_style=(long)value;}
    else if(!strcmp(selector,"setAccessoryType:")){if(value>4)stop("Invalid table cell accessory type",selector);cell->accessory_type=(long)value;}
    else if(!strcmp(selector,"setIndentationLevel:")){if((long)value<0)stop("Invalid table cell indentation",selector);cell->indentation=(long)value;}
    else{Word *slot=!strcmp(selector,"setAccessoryView:") ? &cell->accessory : !strcmp(selector,"setBackgroundView:") ? &cell->background : &cell->selected_background;if(*slot)view_remove(*slot);view_strong_property(slot,value);if(value){view_add(object,selector,value);if(slot!=&cell->accessory)view_move_child(object,selector,((struct Sequence *)view_state(object)->children)->count-1,0);}}
    table_cell_layout(object,"layoutSubviews");
}
static double table_cell_indent(Word object) { return table_cell_state(object)->indentation_width; }
static void table_cell_set_indent(Word object,const char *selector,double width) { if(!isfinite(width) || width<0)stop("Invalid table cell indentation width",selector);table_cell_state(object)->indentation_width=width;table_cell_layout(object,"layoutSubviews"); }
static void table_cell_prepare(Word object,const char *selector) { table_cell_set_selection(object,"setSelected:",0,0);table_cell_set_selection(object,"setHighlighted:",0,0);view_state(object)->alpha=1; }
static void table_recycle(struct TableState *table,Word object) {
    struct TableRow *row=table_row_object(table,object);if(!row || !row->visible)return;
    table_callbacks(table);row->visible=0;row->queued=row->cell && table_cell_state(object)->identifier!=0;
    table_row_display(table,row,0);
    view_remove(object);
    if(!row->queued){struct TableRow **link=&table->rows;while(*link && *link!=row)link=&(*link)->next;if(*link)*link=row->next;if(row->cell)table_cell_state(object)->table=NULL;metal_object_release(object);free(row);}
}
static Word table_new_registered(struct TableState *table,const char *selector,Word identifier) {
    for(struct TableRegistration *entry=table->registrations;entry;entry=entry->next)if((long)string_compare(entry->identifier,"compare:",identifier)==0){Word result=host_allocate(entry->cls);Method method=find_implementation(result,"initWithStyle:reuseIdentifier:");if(!method)stop("Missing registered cell initializer",selector);return method(result,"initWithStyle:reuseIdentifier:",0,identifier,0,0);}return 0;
}
static Word table_dequeue(Word object,const char *selector,Word identifier) {
    if(!identifier)stop("A table reuse identifier is required",selector);struct TableState *table=table_state(object);
    for(struct TableRow *row=table->rows;row;row=row->next)if(row->queued && row->cell){struct TableCellState *cell=table_cell_state(row->object);if(cell->identifier && (long)string_compare(cell->identifier,"compare:",identifier)==0){row->queued=0;unsigned epoch=__atomic_load_n(&method_cache_epoch,__ATOMIC_ACQUIRE);if(!cell->cached || cell->epoch!=epoch){cell->prepare=find_implementation(row->object,"prepareForReuse");cell->layout=find_implementation(row->object,"layoutSubviews");cell->cached=1;cell->epoch=epoch;}if(cell->prepare)cell->prepare(row->object,"prepareForReuse",0,0,0,0);return row->object;}}
    return table_new_registered(table,selector,identifier);
}
static struct TableRegistration **table_registrations(struct TableState *table) { return &table->registrations; }
static void table_register(Word object,const char *selector,Word cls,Word identifier) {
    if(!identifier || (cls && !class_inherits(cls,external_class("_OBJC_CLASS_$_UITableViewCell"))))stop("Invalid table cell registration",selector);struct TableRegistration **link=table_registrations(table_state(object));
    while(*link && (long)string_compare((*link)->identifier,"compare:",identifier)!=0)link=&(*link)->next;
    if(!*link && cls){*link=calloc(1,sizeof(**link));if(!*link)stop("Allocation failed",selector);(*link)->identifier=text_copy(identifier,"copy");}
    if(*link){if(cls)(*link)->cls=cls;else{struct TableRegistration *entry=*link;*link=entry->next;metal_object_release(entry->identifier);free(entry);}}
}
static Word table_dequeue_path(Word object,const char *selector,Word identifier,Word path) {
    struct TableState *table=table_state(object);table_position_for_path(table,path,selector);Word cell=table_dequeue(object,selector,identifier);if(cell)return cell;
    stop("No table cell registered for this reuse identifier",selector);return 0;
}
static Word table_section_view(struct TableState *table,struct TablePosition position) {
    if(position.kind==3)return table->header;if(position.kind==4)return table->footer;struct TableSection *section=&table->sections[position.section];Word *slot=position.kind==1 ? &section->header_view : &section->footer_view;
    if(!*slot){unsigned callback=position.kind==1 ? 3 : 4;if(table->delegate_methods[callback])*slot=metal_object_retain(table->delegate_methods[callback](table->delegate,table_delegate_callbacks[callback],table->object,(Word)position.section,0,0));
        if(!*slot){*slot=view_init_frame(host_allocate(external_class("_OBJC_CLASS_$_UIView")),"Table section",(struct GuestRect){0});Word label=view_init_frame(host_allocate(external_class("_OBJC_CLASS_$_UILabel")),"Table section title",(struct GuestRect){16,0,fmax(0,view_state(table->object)->bounds.width-32),position.height});label_font(label,"setFont:",ui_font_system(external_class("_OBJC_CLASS_$_UIFont"),"systemFontOfSize:",14));label_text(label,"setText:",position.kind==1 ? section->header_title : section->footer_title);view_add(*slot,"Table section title",label);}}
    return *slot;
}
static Word table_bind(struct TableState *table,long index,Word previous,double width,double *height,unsigned *kind) {
    if(previous)table_recycle(table,previous);table_refresh(table);table_callbacks(table);struct TablePosition position=table_position(table,index);Word object;table->binding=1;
    if(!position.kind){if(!table->source_methods[2])stop("Missing table data source method",table_source_callbacks[2]);Word path=table_path(position.section,position.row);object=table->source_methods[2](table->source,table_source_callbacks[2],table->object,path,0,0);if(!object || !class_inherits(read_word(object),external_class("_OBJC_CLASS_$_UITableViewCell")))stop("Table data source returned an invalid cell",table_source_callbacks[2]);
        position.height=table->row_height;if(table->delegate_methods[0])position.height=((double (*)(Word,const char *,Word,Word))table->delegate_methods[0])(table->delegate,table_delegate_callbacks[0],table->object,path);
        if(position.height==-1){Method size=find_implementation(object,"sizeThatFits:");if(!size || size==(Method)view_size_that_fits)stop("Self-sizing table rows require a sizing implementation","sizeThatFits:");struct GuestSize measured=((struct GuestSize (*)(Word,const char *,struct GuestSize))size)(object,"sizeThatFits:",(struct GuestSize){width,INFINITY});position.height=measured.height;}
        table_cell_state(object)->table=table;table_cell_set_selection(object,"setSelected:",table_selected(table,position.section,position.row),0);
    }else object=table_section_view(table,position);
    if(!object || !class_inherits(read_word(object),external_class("_OBJC_CLASS_$_UIView")))stop("Invalid table supplementary view","UITableView");
    if(!isfinite(position.height) || position.height<0)stop("Invalid table row height","UITableView");
    struct TableRow *row=table_row_object(table,object);if(!row){row=calloc(1,sizeof(*row));if(!row)stop("Allocation failed","Visible table row");row->object=metal_object_retain(object);row->next=table->rows;table->rows=row;}else if(row->visible && row->position!=index)stop("The same table cell was returned for two visible rows",table_source_callbacks[2]);
    row->position=index;row->section=position.section;row->index=position.row;row->kind=position.kind;row->visible=1;row->queued=0;row->cell=position.kind==0;struct ViewState *view=view_state(object);view->host_container=object;view->hidden=0;table_cell_frame(object,(struct GuestRect){0,position.origin,width,position.height});
    if(view->parent!=table->object){if(view->parent)sequence_remove((void *)view_state(view->parent)->children,"Table reparenting",object);view->parent=table->object;sequence_add((void *)view_state(table->object)->children,"Visible table row",object);}
    if(row->cell){struct TableCellState *cell=table_cell_state(object);table_cell_layout(object,"layoutSubviews");unsigned epoch=__atomic_load_n(&method_cache_epoch,__ATOMIC_ACQUIRE);if(!cell->cached || cell->epoch!=epoch){cell->prepare=find_implementation(object,"prepareForReuse");cell->layout=find_implementation(object,"layoutSubviews");cell->epoch=epoch;cell->cached=1;}if(cell->layout && cell->layout!=(Method)table_cell_layout)cell->layout(object,"layoutSubviews",0,0,0,0);view_layout_if_needed(object,"Table row layout");}
    table_measure(table,index,position.height,position.kind ? position.height : table_default_height(table));view_sync_subtree(object);
    *height=position.height;*kind=position.kind;table->binding=0;return object;
}
static void table_row_display(struct TableState *table,struct TableRow *row,unsigned displayed) {
    if(row->displayed==displayed)return;row->displayed=displayed;table_callbacks(table);
    if(row->cell){unsigned callback=displayed ? 5 : 6;if(table->delegate_methods[callback])table->delegate_methods[callback](table->delegate,table_delegate_callbacks[callback],table->object,row->object,table_path(row->section,row->index),0);}
    else if(displayed && (row->kind==1 || row->kind==2)){unsigned callback=row->kind==1 ? 17 : 18;if(table->delegate_methods[callback])table->delegate_methods[callback](table->delegate,table_delegate_callbacks[callback],table->object,row->object,(Word)row->section,0);}
}
static void ui_table_sync(Word object) {
    struct ViewState *view=view_state(object);struct TableState *table=view->table;if(!table || table->closed || table->refreshing || table->binding)return;float metrics[18]={0};int options[14]={0};Word owner=view_host_geometry(object,metrics,options);
    if(options[1])table_refresh(table);struct ScrollState *scroll=scroll_state(object);double scale=screen_scale();metrics[11]=scale;metrics[12]=scroll->insets.top*scale;metrics[13]=scroll->insets.left*scale;metrics[14]=scroll->insets.bottom*scale;metrics[15]=scroll->insets.right*scale;metrics[16]=table->separator.left*scale;metrics[17]=table->separator.right*scale;
    options[3]=(int)label_argb(view->background,0);options[4]=(int)table->total;options[5]=(int)label_argb(table->separator_color,0xffc6c6c8);options[6]=table->separator_style;options[7]=table->selection;options[8]=table->multiple;options[9]=table->generation;options[10]=table->style;options[11]=scroll->enabled;options[12]=scroll->vertical;options[13]=scroll->horizontal;
    if(table->background){struct ViewState *background=view_state(table->background);background->host_container=table->background;background->parent=object;table_cell_frame(table->background,(struct GuestRect){0,0,view->bounds.width,view->bounds.height});}
    if(table->synced && table->synced_owner==owner && !memcmp(table->metrics,metrics,sizeof(metrics)) && !memcmp(table->options,options,sizeof(options)))return;
    if(!options[1] && !table->synced)return;JNIEnv *env=attached_environment("Table geometry");static jmethodID method;if(!method){jclass cls=(*env)->GetObjectClass(env,activity);method=(*env)->GetMethodID(env,cls,"syncGuestTable","(JJJ[F[I)V");(*env)->DeleteLocalRef(env,cls);}
    jfloatArray m=(*env)->NewFloatArray(env,COUNT(metrics));jintArray o=(*env)->NewIntArray(env,COUNT(options));(*env)->SetFloatArrayRegion(env,m,0,COUNT(metrics),metrics);(*env)->SetIntArrayRegion(env,o,0,COUNT(options),options);(*env)->CallVoidMethod(env,activity,method,(jlong)object,(jlong)owner,(jlong)table->background,m,o);(*env)->DeleteLocalRef(env,m);(*env)->DeleteLocalRef(env,o);
    if((*env)->ExceptionCheck(env)){(*env)->ExceptionClear(env);stop("Android table geometry failed","UITableView");}table->synced=1;table->synced_owner=owner;memcpy(table->metrics,metrics,sizeof(metrics));memcpy(table->options,options,sizeof(options));
    if(table->background)view_sync_subtree(table->background);
}
static void table_invalidate(struct TableState *table) { table->dirty=1;if(!table->updates)ui_table_sync(table->object); }
static Word table_init(Word object,const char *selector,struct GuestRect frame,long style) { if(style<0 || style>2)stop("Invalid table style",selector);table_state(object)->style=style;view_init_frame(object,selector,frame);ui_table_sync(object);return object; }
static Word table_init_frame(Word object,const char *selector,struct GuestRect frame) { return table_init(object,selector,frame,0); }
static Word table_init_plain(Word object,const char *selector) { return table_init(object,selector,(struct GuestRect){0},0); }
static void table_reload(Word object,const char *selector) { struct TableState *table=table_state(object);table->cached=0;struct Sequence *selected=(void *)table->selected;selected->count=0;++selected->mutation;table_invalidate(table); }
static Word table_sections(Word object,const char *selector) { struct TableState *table=table_state(object);table_refresh(table);return table->section_count; }
static Word table_number_rows(Word object,const char *selector,long section) { struct TableState *table=table_state(object);table_refresh(table);if(section<0 || section>=table->section_count)stop("Table section out of range",selector);return table->sections[section].rows; }
static Word table_property(Word object,const char *selector) {
    struct TableState *table=table_state(object);if(!strcmp(selector,"dataSource"))return table->source;if(!strcmp(selector,"delegate"))return table->delegate;if(!strcmp(selector,"style"))return table->style;if(!strcmp(selector,"tableHeaderView"))return table->header;if(!strcmp(selector,"tableFooterView"))return table->footer;if(!strcmp(selector,"backgroundView"))return table->background;if(!strcmp(selector,"separatorColor"))return table->separator_color;if(!strcmp(selector,"separatorStyle"))return table->separator_style;if(!strcmp(selector,"allowsSelection"))return table->selection;if(!strcmp(selector,"allowsMultipleSelection"))return table->multiple;if(!strcmp(selector,"isDragging"))return table->dragging;if(!strcmp(selector,"isDecelerating"))return table->decelerating;return table->readable;
}
static void table_set_property(Word object,const char *selector,Word value) {
    struct TableState *table=table_state(object);if(!strcmp(selector,"setDataSource:")){store_weak(&table->source,value);table->cached=0;table_invalidate(table);return;}if(!strcmp(selector,"setDelegate:")){store_weak(&table->delegate,value);ui_scroll_input_delegate(object,value);table->cached=0;table_invalidate(table);return;}
    if(!strcmp(selector,"setAllowsSelection:"))table->selection=value!=0;else if(!strcmp(selector,"setAllowsMultipleSelection:"))table->multiple=value!=0;else if(!strcmp(selector,"setCellLayoutMarginsFollowReadableWidth:")){table->readable=value!=0;for(struct TableRow *row=table->rows;row;row=row->next)if(row->visible && row->cell)table_cell_layout(row->object,"layoutSubviews");}
    else if(!strcmp(selector,"setSeparatorStyle:")){if(value>2)stop("Invalid table separator style",selector);table->separator_style=(long)value;}
    else{Word *slot=!strcmp(selector,"setTableHeaderView:") ? &table->header : !strcmp(selector,"setTableFooterView:") ? &table->footer : !strcmp(selector,"setBackgroundView:") ? &table->background : &table->separator_color;if(*slot && slot!=&table->separator_color)view_remove(*slot);view_strong_property(slot,value);if(slot!=&table->separator_color){table->synced=0;table_invalidate(table);return;}}
    ui_table_sync(object);
}
static double table_height_property(Word object,const char *selector) { struct TableState *table=table_state(object);if(!strcmp(selector,"rowHeight"))return table->row_height;if(!strcmp(selector,"estimatedRowHeight"))return table->estimated_height;if(!strcmp(selector,"sectionHeaderHeight"))return table->header_height;return table->footer_height; }
static void table_set_height(Word object,const char *selector,double value) { if(!isfinite(value) || (value<0 && value!=-1))stop("Invalid table height",selector);struct TableState *table=table_state(object);if(!strcmp(selector,"setRowHeight:"))table->row_height=value;else if(!strcmp(selector,"setEstimatedRowHeight:"))table->estimated_height=value;else if(!strcmp(selector,"setSectionHeaderHeight:"))table->header_height=value;else table->footer_height=value;table_invalidate(table); }
static struct GuestInsets table_separator(Word object) { return table_state(object)->separator; }
static void table_set_separator(Word object,const char *selector,struct GuestInsets value) { if(!isfinite(value.top) || !isfinite(value.left) || !isfinite(value.bottom) || !isfinite(value.right))stop("Invalid table separator insets",selector);table_state(object)->separator=value;ui_table_sync(object); }
static struct GuestInsets table_cell_separator(Word object) { return table_cell_state(object)->separator; }
static void table_cell_set_separator(Word object,const char *selector,struct GuestInsets value) { if(!isfinite(value.top) || !isfinite(value.left) || !isfinite(value.bottom) || !isfinite(value.right))stop("Invalid cell separator insets",selector);struct TableCellState *cell=table_cell_state(object);cell->separator=value;cell->separator_set=1;table_cell_appearance(cell); }
static Word table_visible(Word object,const char *selector) {
    struct TableState *table=table_state(object);struct Sequence *result=(void *)sequence_empty(external_class("_OBJC_CLASS_$_NSArray"));int paths=!strcmp(selector,"indexPathsForVisibleRows");
    for(long last=-1;;){struct TableRow *next=NULL;for(struct TableRow *row=table->rows;row;row=row->next)if(row->displayed && row->cell && row->position>last && (!next || row->position<next->position))next=row;if(!next)break;last=next->position;sequence_add(result,selector,paths ? table_path(next->section,next->index) : next->object);}return (Word)result;
}
static Word table_cell_at_path(Word object,const char *selector,Word path) { struct TableState *table=table_state(object);long position=table_position_for_path(table,path,selector);for(struct TableRow *row=table->rows;row;row=row->next)if(row->displayed && row->position==position && row->cell)return row->object;return 0; }
static Word table_path_for_cell(Word object,const char *selector,Word cell) { struct TableState *table=table_state(object);struct TableRow *row=table_row_object(table,cell);if(!row || !row->visible || !row->cell)return 0;return table_path(row->section,row->index); }
static Word table_selected_paths(Word object,const char *selector) { struct TableState *table=table_state(object);struct Sequence *paths=(void *)table->selected;return !strcmp(selector,"indexPathForSelectedRow") ? paths->count ? paths->items[0] : 0 : paths->count ? sequence_copy(paths,"copy") : 0; }
static void table_apply_selection(struct TableState *table) { for(struct TableRow *row=table->rows;row;row=row->next)if(row->visible && row->cell){struct TablePosition position=table_position(table,row->position);table_cell_set_selection(row->object,"setSelected:",table_selected(table,position.section,position.row),0);} }
static void table_select(Word object,const char *selector,Word path,Word animated,long scroll_position) {
    if(scroll_position<0 || scroll_position>3)stop("Invalid table scroll position",selector);struct TableState *table=table_state(object);struct Sequence *paths=(void *)table->selected;if(!path){paths->count=0;++paths->mutation;table_apply_selection(table);return;}long position=table_position_for_path(table,path,selector);long section=table_path_component(path,0,selector),row=table_path_component(path,1,selector);
    if(!table->multiple){paths->count=0;++paths->mutation;}if(!table_selected(table,section,row))sequence_add(paths,selector,table_path(section,row));table_apply_selection(table);if(scroll_position)table_command(table,1,(int)position,0,animated,(int)scroll_position);
}
static void table_deselect(Word object,const char *selector,Word path,Word animated) { if(!path)return;struct TableState *table=table_state(object);long section=table_path_component(path,0,selector),row=table_path_component(path,1,selector);struct Sequence *paths=(void *)table->selected;for(size_t i=0;i<paths->count;++i)if(table_path_component(paths->items[i],0,selector)==section && table_path_component(paths->items[i],1,selector)==row){sequence_remove(paths,selector,paths->items[i]);break;}table_apply_selection(table); }
static void table_scroll_row(Word object,const char *selector,Word path,long position,Word animated) { if(position<0 || position>3)stop("Invalid table scroll position",selector);struct TableState *table=table_state(object);table_command(table,1,(int)table_position_for_path(table,path,selector),0,animated,(int)position);table->animated=animated!=0; }
static struct GuestSize table_content_size(Word object) { struct TableState *table=table_state(object);table_refresh(table);return scroll_state(object)->size; }
static struct GuestRect table_row_rect(Word object,const char *selector,Word path) { struct TableState *table=table_state(object);long index=table_position_for_path(table,path,selector);struct TablePosition position=table_position(table,index);size_t height=table_height_index(table,index);if(height<table->height_count && table->heights[height].position==index)position.height+=table->heights[height].delta;return (struct GuestRect){0,position.origin,view_state(object)->bounds.width,position.height}; }
static Word table_path_at_point(Word object,const char *selector,struct GuestPoint point) { struct TableState *table=table_state(object);table_refresh(table);if(point.x<0 || point.x>=view_state(object)->bounds.width || point.y<0)return 0;long low=0,high=table->total;while(low<high){long middle=low+(high-low)/2;struct TablePosition position=table_position(table,middle);if(position.origin<=point.y)low=middle+1;else high=middle;}if(!low)return 0;long index=low-1;struct TablePosition position=table_position(table,index);if(position.kind)return 0;size_t measured=table_height_index(table,index);double height=position.height;if(measured<table->height_count && table->heights[measured].position==index)height+=table->heights[measured].delta;return point.y<position.origin+height ? table_path(position.section,position.row) : 0; }
static void ui_table_scroll(Word object,unsigned animated) {
    struct TableState *table=view_state(object)->table;if(!table || table->closed)return;table_refresh(table);struct ScrollState *scroll=scroll_state(object);long low=0,high=table->total;
    while(low<high){long middle=low+(high-low)/2;struct TablePosition position=table_position(table,middle);if(position.origin<=scroll->offset.y)low=middle+1;else high=middle;}
    long index=low ? low-1 : 0;if(!table->total)return;struct TablePosition position=table_position(table,index);table_command(table,2,(int)index,(float)((position.origin-scroll->offset.y)*screen_scale()),animated,0);table->animated=animated!=0;
}
static void table_offset_animated(Word object,const char *selector,struct GuestPoint point,Word animated) { struct ScrollState *scroll=scroll_state(object);scroll->animated=animated!=0;scroll_set_offset(object,selector,point);scroll->animated=0; }
static void table_begin_updates(Word object,const char *selector) { struct TableState *table=table_state(object);if(table->updates==UINT_MAX)stop("Unbalanced table updates",selector);++table->updates; }
static void table_end_updates(Word object,const char *selector) { struct TableState *table=table_state(object);if(!table->updates)stop("Unbalanced table updates",selector);if(!--table->updates)table_invalidate(table); }
static void table_reload_rows(Word object,const char *selector,Word paths,Word animation) { if(!paths)stop("Table row index paths are required",selector);struct TableState *table=table_state(object);struct Sequence *items=(void *)paths;for(size_t i=0;i<items->count;++i)table_position_for_path(table,items->items[i],selector);table_invalidate(table); }
static Word table_controller_sections(Word object,const char *selector,Word table) { return 1; }
static Word table_controller_rows(Word object,const char *selector,Word table,Word section) { return 0; }
static void ui_table_controller_load(Word controller,Word view) { if(!class_inherits(read_word(controller),external_class("_OBJC_CLASS_$_UITableViewController")) || !class_inherits(read_word(view),external_class("_OBJC_CLASS_$_UITableView")))return;struct TableState *table=table_state(view);table->style=view_state(controller)->bar_style;store_weak(&table->source,controller);store_weak(&table->delegate,controller);ui_scroll_input_delegate(view,controller);table->cached=0;table->dirty=1; }
static void ui_table_controller_appear(Word controller) { if(!class_inherits(read_word(controller),external_class("_OBJC_CLASS_$_UITableViewController")) || !view_state(controller)->clears_selection)return;Word view=controller_loaded_view(controller);if(view && view_state(view)->table)table_select(view,"selectRowAtIndexPath:animated:scrollPosition:",0,0,0); }
static void table_dealloc(Word object,const char *selector) {
    struct TableState *table=view_state(object)->table;if(!table)return;table_command(table,4,0,0,0,0);table->closed=1;view_remove(object);table_free_sections(table);
    while(table->rows){struct TableRow *row=table->rows;table->rows=row->next;view_remove(row->object);if(row->cell)table_cell_state(row->object)->table=NULL;metal_object_release(row->object);free(row);}
    struct TableRegistration **registrations=table_registrations(table);while(*registrations){struct TableRegistration *entry=*registrations;*registrations=entry->next;metal_object_release(entry->identifier);free(entry);}metal_object_release(table->header);metal_object_release(table->footer);metal_object_release(table->background);metal_object_release(table->separator_color);metal_object_release(table->selected);destroy_weak(&table->source);destroy_weak(&table->delegate);free(table->heights);view_state(object)->table=NULL;free(table);
}
JNIEXPORT jlongArray JNICALL HOST_JNI(HostActivity_bindGuestTableRow)(JNIEnv *env,jobject instance,jlong token,jint position,jlong previous,jfloat width) {
    struct TableState *table=view_state(token)->table;if(!table || table->closed)return NULL;may_unwind=1;int failed=setjmp(boundary);jlongArray result=NULL;
    if(!failed){double height=0;unsigned kind=0;Word object=table_bind(table,position,previous,width/screen_scale(),&height,&kind);float pixels=(float)(height*screen_scale());uint32_t bits;memcpy(&bits,&pixels,sizeof(bits));struct TableCellState *cell=kind ? NULL : table_cell_state(object);jlong values[]={(jlong)object,bits,kind,cell && !cell->accessory ? cell->accessory_type : 0,cell ? cell->selection_style : 0,cell ? cell->selected : 0};result=(*env)->NewLongArray(env,COUNT(values));if(result)(*env)->SetLongArrayRegion(env,result,0,COUNT(values),values);}
    may_unwind=0;if(failed){table->binding=0;table->refreshing=0;input_report_failure(env);}return result;
}
JNIEXPORT jstring JNICALL HOST_JNI(HostActivity_guestTableEvent)(JNIEnv *env,jobject instance,jlong token,jint event,jint index,jlong object,jfloat top,jfloat width,jint state) {
    struct TableState *table=view_state(token)->table;if(!table || table->closed)return NULL;may_unwind=1;int failed=setjmp(boundary);
    if(!failed){table_callbacks(table);
        if(event==2)table_recycle(table,object);
        else if(event==5 || event==6){struct TableRow *row=table_row_object(table,object);if(row && row->visible)table_row_display(table,row,event==5);}
        else if(event==3 && index>=0 && index<table->total){struct TablePosition position=table_position(table,index);struct ScrollState *scroll=scroll_state(token);double y=position.origin-top/screen_scale();int changed=fabs(scroll->offset.y-y)>0.001;scroll->offset.y=y;view_state(token)->bounds.y=y;scroll->size.width=width/screen_scale();if(changed)scroll_notify(token);}
        else if(event==4){unsigned previous_state=table->dragging ? 1 : table->decelerating ? 2 : 0;if(state==1 && previous_state!=1 && table->delegate_methods[12])table->delegate_methods[12](table->delegate,table_delegate_callbacks[12],token,0,0,0);
            if(previous_state==1 && state!=1 && table->delegate_methods[13])table->delegate_methods[13](table->delegate,table_delegate_callbacks[13],token,state==2,0,0);
            if(state==2 && previous_state!=2 && table->delegate_methods[14])table->delegate_methods[14](table->delegate,table_delegate_callbacks[14],token,0,0,0);
            if(!state && previous_state==2 && table->delegate_methods[15])table->delegate_methods[15](table->delegate,table_delegate_callbacks[15],token,0,0,0);
            if(!state && table->animated){table->animated=0;if(table->delegate_methods[16])table->delegate_methods[16](table->delegate,table_delegate_callbacks[16],token,0,0,0);}table->dragging=state==1;table->decelerating=state==2;
        }else if((event==0 || event==1) && index>=0 && index<table->total){struct TablePosition position=table_position(table,index);if(!position.kind){Word path=table_path(position.section,position.row);
            if(event==1){if(table->delegate_methods[11])table->delegate_methods[11](table->delegate,table_delegate_callbacks[11],token,path,0,0);}
            else if(table->selection){int selected=table->multiple && table_selected(table,position.section,position.row);unsigned before=selected ? 9 : 7,after=selected ? 10 : 8;if(table->delegate_methods[before])path=table->delegate_methods[before](table->delegate,table_delegate_callbacks[before],token,path,0,0);if(path){
                if(selected)table_deselect(token,"deselectRowAtIndexPath:animated:",path,1);
                else{struct Sequence *selection=(void *)table->selected;Word previous=!table->multiple && selection->count ? selection->items[0] : 0;
                    if(previous && (table_path_component(previous,0,"Selection")!=table_path_component(path,0,"Selection") || table_path_component(previous,1,"Selection")!=table_path_component(path,1,"Selection"))){if(table->delegate_methods[9])previous=table->delegate_methods[9](table->delegate,table_delegate_callbacks[9],token,previous,0,0);if(previous){table_deselect(token,"deselectRowAtIndexPath:animated:",previous,1);if(table->delegate_methods[10])table->delegate_methods[10](table->delegate,table_delegate_callbacks[10],token,previous,0,0);}}
                    table_select(token,"selectRowAtIndexPath:animated:scrollPosition:",path,1,0);}
                if(table->delegate_methods[after])table->delegate_methods[after](table->delegate,table_delegate_callbacks[after],token,path,0,0);}}
        }}
    }
    may_unwind=0;if(failed){table->refreshing=0;table->binding=0;}return failed ? (*env)->NewStringUTF(env,status) : NULL;
}
static Method ui_tables_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if(!receiver || class_receiver)return NULL;Word cls=read_word(object);
    if(class_inherits(cls,external_class("_OBJC_CLASS_$_UITableViewCell"))){
        if(!strcmp(selector,"initWithStyle:reuseIdentifier:"))return (Method)table_cell_init;if(!strcmp(selector,"init"))return (Method)table_cell_init_plain;if(!strcmp(selector,"initWithFrame:"))return (Method)table_cell_init_frame;if(!strcmp(selector,"contentView"))return (Method)table_cell_content;if(!strcmp(selector,"textLabel") || !strcmp(selector,"detailTextLabel"))return (Method)table_cell_label;if(!strcmp(selector,"imageView"))return (Method)table_cell_image;
        if(!strcmp(selector,"setSelected:") || !strcmp(selector,"setSelected:animated:") || !strcmp(selector,"setHighlighted:") || !strcmp(selector,"setHighlighted:animated:"))return (Method)table_cell_set_selection;
        if(!strcmp(selector,"indentationWidth"))return (Method)table_cell_indent;if(!strcmp(selector,"setIndentationWidth:"))return (Method)table_cell_set_indent;if(!strcmp(selector,"separatorInset"))return (Method)table_cell_separator;if(!strcmp(selector,"setSeparatorInset:"))return (Method)table_cell_set_separator;if(!strcmp(selector,"prepareForReuse"))return (Method)table_cell_prepare;if(!strcmp(selector,"layoutSubviews"))return (Method)table_cell_layout;
        const char *getters[]={"reuseIdentifier","selectionStyle","isSelected","isHighlighted","accessoryType","accessoryView","backgroundView","selectedBackgroundView","indentationLevel"};for(unsigned i=0;i<COUNT(getters);++i)if(!strcmp(selector,getters[i]))return (Method)table_cell_property;
        const char *setters[]={"setSelectionStyle:","setAccessoryType:","setAccessoryView:","setBackgroundView:","setSelectedBackgroundView:","setIndentationLevel:"};for(unsigned i=0;i<COUNT(setters);++i)if(!strcmp(selector,setters[i]))return (Method)table_cell_set_property;
    }
    if(class_inherits(cls,external_class("_OBJC_CLASS_$_UITableView"))){
        if(!strcmp(selector,"initWithFrame:style:"))return (Method)table_init;if(!strcmp(selector,"initWithFrame:"))return (Method)table_init_frame;if(!strcmp(selector,"init"))return (Method)table_init_plain;if(!strcmp(selector,"reloadData"))return (Method)table_reload;if(!strcmp(selector,"numberOfSections"))return (Method)table_sections;if(!strcmp(selector,"numberOfRowsInSection:"))return (Method)table_number_rows;
        if(!strcmp(selector,"dequeueReusableCellWithIdentifier:"))return (Method)table_dequeue;if(!strcmp(selector,"dequeueReusableCellWithIdentifier:forIndexPath:"))return (Method)table_dequeue_path;if(!strcmp(selector,"registerClass:forCellReuseIdentifier:"))return (Method)table_register;
        if(!strcmp(selector,"visibleCells") || !strcmp(selector,"indexPathsForVisibleRows"))return (Method)table_visible;if(!strcmp(selector,"cellForRowAtIndexPath:"))return (Method)table_cell_at_path;if(!strcmp(selector,"indexPathForCell:"))return (Method)table_path_for_cell;if(!strcmp(selector,"indexPathForSelectedRow") || !strcmp(selector,"indexPathsForSelectedRows"))return (Method)table_selected_paths;
        if(!strcmp(selector,"selectRowAtIndexPath:animated:scrollPosition:"))return (Method)table_select;if(!strcmp(selector,"deselectRowAtIndexPath:animated:"))return (Method)table_deselect;if(!strcmp(selector,"scrollToRowAtIndexPath:atScrollPosition:animated:"))return (Method)table_scroll_row;if(!strcmp(selector,"setContentOffset:animated:"))return (Method)table_offset_animated;if(!strcmp(selector,"contentSize"))return (Method)table_content_size;
        if(!strcmp(selector,"rectForRowAtIndexPath:"))return (Method)table_row_rect;if(!strcmp(selector,"indexPathForRowAtPoint:"))return (Method)table_path_at_point;if(!strcmp(selector,"beginUpdates"))return (Method)table_begin_updates;if(!strcmp(selector,"endUpdates"))return (Method)table_end_updates;if(!strcmp(selector,"reloadRowsAtIndexPaths:withRowAnimation:"))return (Method)table_reload_rows;
        if(!strcmp(selector,"separatorInset"))return (Method)table_separator;if(!strcmp(selector,"setSeparatorInset:"))return (Method)table_set_separator;
        const char *getters[]={"dataSource","delegate","style","tableHeaderView","tableFooterView","backgroundView","separatorColor","separatorStyle","allowsSelection","allowsMultipleSelection","cellLayoutMarginsFollowReadableWidth","isDragging","isDecelerating"};for(unsigned i=0;i<COUNT(getters);++i)if(!strcmp(selector,getters[i]))return (Method)table_property;
        const char *setters[]={"setDataSource:","setDelegate:","setTableHeaderView:","setTableFooterView:","setBackgroundView:","setSeparatorColor:","setSeparatorStyle:","setAllowsSelection:","setAllowsMultipleSelection:","setCellLayoutMarginsFollowReadableWidth:"};for(unsigned i=0;i<COUNT(setters);++i)if(!strcmp(selector,setters[i]))return (Method)table_set_property;
        const char *heights[]={"rowHeight","estimatedRowHeight","sectionHeaderHeight","sectionFooterHeight"};for(unsigned i=0;i<COUNT(heights);++i)if(!strcmp(selector,heights[i]))return (Method)table_height_property;const char *height_setters[]={"setRowHeight:","setEstimatedRowHeight:","setSectionHeaderHeight:","setSectionFooterHeight:"};for(unsigned i=0;i<COUNT(height_setters);++i)if(!strcmp(selector,height_setters[i]))return (Method)table_set_height;
        if(!strcmp(selector,"dealloc"))return (Method)table_dealloc;
    }
    if(class_inherits(cls,external_class("_OBJC_CLASS_$_UITableViewController"))){if(!strcmp(selector,"numberOfSectionsInTableView:"))return (Method)table_controller_sections;if(!strcmp(selector,"tableView:numberOfRowsInSection:"))return (Method)table_controller_rows;}
    return NULL;
}
