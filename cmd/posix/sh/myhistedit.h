#ifndef MYHISTEDIT_H_
#define MYHISTEDIT_H_

#if FEATURE_SH_HISTEDIT

#include <histedit.h>
#include <filecomplete.h>

extern History *hist;
extern EditLine *el;

void histedit(void);
void sethistsize(const char *);
void setterm(const char *);
void histload(void);
void histsave(void);

#else

/* stubbed histedit definitions */
typedef void History;
typedef void EditLine;

extern History *hist;
extern EditLine *el;

#define histedit() ((void)0)
#define sethistsize(s) ((void)0)
#define setterm(t) ((void)0)
#define histload() ((void)0)
#define histsave() ((void)0)

#endif

extern int displayhist;

#endif /* !MYHISTEDIT_H_ */
